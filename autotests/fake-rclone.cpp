/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <thread>
#include <unistd.h>

namespace
{
constexpr qint64 PayloadSize = 16 * 1024 * 1024;
constexpr qsizetype ChunkSize = 16 * 1024;
constexpr auto RcatBlockFile = ".kio-rclone-test-block-rcat";
constexpr auto RcatFailFile = ".kio-rclone-test-fail-rcat";
constexpr auto RcatStartedFile = ".kio-rclone-test-rcat-started";
constexpr auto CopytoBlockFile = ".kio-rclone-test-block-copyto";
constexpr auto CopytoFailFile = ".kio-rclone-test-fail-copyto";
constexpr auto CopytoStartedFile = ".kio-rclone-test-copyto-started";
constexpr auto LogicalItemsEnvironment = "KIO_RCLONE_TEST_LOGICAL_ITEMS";

struct RemotePath {
    QString remote;
    QString relativePath;
    QString localPath;
    bool valid = false;
};

struct LogicalItem {
    QString remote;
    QString path;
    QString name;
    QString id;
    QString mimeType;
    QString sourcePath;
    qint64 size = -1;
    QDateTime modificationTime;
    bool isDirectory = false;
};

bool writeAll(int descriptor, const char *data, qsizetype size)
{
    qsizetype offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(descriptor, data + offset, size_t(size - offset));
        if (written > 0) {
            offset += written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool writeAll(QFile &file, const char *data, qsizetype size)
{
    qsizetype offset = 0;
    while (offset < size) {
        const qint64 written = file.write(data + offset, size - offset);
        if (written <= 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

int writeError(const QByteArray &message)
{
    const QByteArray line = message.endsWith('\n') ? message : message + '\n';
    writeAll(STDERR_FILENO, line.constData(), line.size());
    return 1;
}

void writeCounter(int descriptor, qint64 value)
{
    if (descriptor >= 0) {
        [[maybe_unused]] const ssize_t written = ::pwrite(descriptor, &value, sizeof(value), static_cast<off_t>(0));
    }
}

void writeUploadStats(qint64 transferred, qint64 totalSize = PayloadSize, const QString &name = QStringLiteral("uploaded.bin"))
{
    const qint64 effectiveSize = totalSize > 0 ? totalSize : qMax<qint64>(transferred, 1);
    const int percentage = int(qMin<qint64>(100, (transferred * 100) / effectiveSize));
    const QJsonObject transfer{
        {QStringLiteral("bytes"), transferred},
        {QStringLiteral("eta"), qMax<qint64>(0, (effectiveSize - transferred) / (1024 * 1024))},
        {QStringLiteral("name"), name},
        {QStringLiteral("percentage"), percentage},
        {QStringLiteral("size"), effectiveSize},
        {QStringLiteral("speed"), 1024 * 1024},
        {QStringLiteral("speedAvg"), 1024 * 1024},
    };
    const QJsonObject stats{
        {QStringLiteral("bytes"), transferred},
        {QStringLiteral("eta"), qMax<qint64>(0, (effectiveSize - transferred) / (1024 * 1024))},
        {QStringLiteral("speed"), 1024 * 1024},
        {QStringLiteral("totalBytes"), effectiveSize},
        {QStringLiteral("transferring"), QJsonArray{transfer}},
    };
    const QJsonObject log{
        {QStringLiteral("level"), QStringLiteral("notice")},
        {QStringLiteral("msg"), QStringLiteral("test upload stats")},
        {QStringLiteral("stats"), stats},
    };
    const QByteArray line = QJsonDocument(log).toJson(QJsonDocument::Compact) + '\n';
    writeAll(STDERR_FILENO, line.constData(), line.size());
}

int openCounter()
{
    const QString path = qEnvironmentVariable("KIO_RCLONE_TEST_TRANSFER_COUNTER");
    if (path.isEmpty()) {
        return -1;
    }
    return ::open(QFile::encodeName(path).constData(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
}

int streamLegacyDownload()
{
    const int counter = openCounter();
    writeCounter(counter, 0);

    const QByteArray chunk(ChunkSize, 'R');
    qint64 transferred = 0;
    while (transferred < PayloadSize) {
        const qsizetype amount = qMin<qint64>(chunk.size(), PayloadSize - transferred);
        if (!writeAll(STDOUT_FILENO, chunk.constData(), amount)) {
            if (counter >= 0) {
                ::close(counter);
            }
            return 1;
        }
        transferred += amount;
        writeCounter(counter, transferred);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (counter >= 0) {
        ::close(counter);
    }
    return 0;
}

int consumeLegacyUpload()
{
    const int counter = openCounter();
    writeCounter(counter, 0);

    std::array<char, ChunkSize> chunk;
    qint64 transferred = 0;
    qint64 nextStats = 1024 * 1024;
    for (;;) {
        const ssize_t received = ::read(STDIN_FILENO, chunk.data(), chunk.size());
        if (received > 0) {
            transferred += received;
            writeCounter(counter, transferred);
            if (transferred >= nextStats) {
                writeUploadStats(transferred);
                nextStats += 1024 * 1024;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (counter >= 0) {
            ::close(counter);
        }
        return received == 0 ? 0 : 1;
    }
}

int writeLegacyStat(const QString &remoteSpec)
{
    if (!remoteSpec.endsWith(QStringLiteral("payload.bin"))) {
        return writeError(QByteArrayLiteral("object not found"));
    }

    QJsonObject item{
        {QStringLiteral("Path"), QStringLiteral("payload.bin")},
        {QStringLiteral("Name"), QStringLiteral("payload.bin")},
        {QStringLiteral("Size"), PayloadSize},
        {QStringLiteral("MimeType"), QStringLiteral("application/octet-stream")},
        {QStringLiteral("ModTime"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("IsDir"), false},
    };
    const QByteArray json = QJsonDocument(item).toJson(QJsonDocument::Compact);
    return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
}

int writeLegacyListing(const QString &remoteSpec)
{
    QJsonArray items;
    if (remoteSpec == QLatin1String("test:")) {
        items.append(QJsonObject{
            {QStringLiteral("Path"), QStringLiteral("payload.bin")},
            {QStringLiteral("Name"), QStringLiteral("payload.bin")},
            {QStringLiteral("Size"), PayloadSize},
            {QStringLiteral("MimeType"), QStringLiteral("application/octet-stream")},
            {QStringLiteral("ModTime"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("IsDir"), false},
        });
    }

    const QByteArray json = QJsonDocument(items).toJson(QJsonDocument::Compact);
    return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
}

RemotePath resolveRemotePath(const QString &root, const QString &remoteSpec)
{
    RemotePath result;
    const qsizetype separator = remoteSpec.indexOf(QLatin1Char(':'));
    if (separator <= 0) {
        return result;
    }

    result.remote = remoteSpec.left(separator);
    const QString rawPath = remoteSpec.mid(separator + 1);
    if (result.remote.contains(QLatin1Char('/')) || result.remote == QLatin1String(".") || result.remote == QLatin1String("..")
        || rawPath.startsWith(QLatin1Char('/'))) {
        return result;
    }

    const QStringList parts = rawPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..")) {
            return result;
        }
    }

    result.relativePath = parts.join(QLatin1Char('/'));
    result.localPath = QDir(root).filePath(result.remote);
    if (!result.relativePath.isEmpty()) {
        result.localPath = QDir(result.localPath).filePath(result.relativePath);
    }
    result.valid = true;
    return result;
}

QString parentPath(const QString &path)
{
    const qsizetype separator = path.lastIndexOf(QLatin1Char('/'));
    return separator < 0 ? QString() : path.left(separator);
}

QList<LogicalItem> readLogicalItems(QString *error)
{
    const QString manifestPath = qEnvironmentVariable(LogicalItemsEnvironment);
    if (manifestPath.isEmpty()) {
        return {};
    }

    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = manifest.errorString();
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = parseError.errorString();
        }
        return {};
    }

    QList<LogicalItem> items;
    const QDir manifestDirectory = QFileInfo(manifestPath).absoluteDir();
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        LogicalItem item;
        item.remote = object.value(QStringLiteral("Remote")).toString();
        item.path = object.value(QStringLiteral("Path")).toString();
        item.name = object.value(QStringLiteral("Name")).toString();
        item.id = object.value(QStringLiteral("ID")).toString();
        item.mimeType = object.value(QStringLiteral("MimeType")).toString();
        item.sourcePath = object.value(QStringLiteral("Source")).toString();
        item.size = object.contains(QStringLiteral("Size")) ? object.value(QStringLiteral("Size")).toVariant().toLongLong() : QFileInfo(item.sourcePath).size();
        item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODateWithMs);
        if (!item.modificationTime.isValid()) {
            item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODate);
        }
        item.isDirectory = object.value(QStringLiteral("IsDir")).toBool();

        if (item.name.isEmpty()) {
            item.name = item.path.section(QLatin1Char('/'), -1);
        }
        if (!QDir::isAbsolutePath(item.sourcePath)) {
            item.sourcePath = manifestDirectory.filePath(item.sourcePath);
        }
        if (item.remote.isEmpty() || item.path.isEmpty() || item.path.startsWith(QLatin1Char('/')) || item.name.isEmpty()
            || item.path.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            if (error) {
                *error = QStringLiteral("invalid logical item");
            }
            return {};
        }
        items.append(item);
    }
    return items;
}

bool preferLogicalItem(const LogicalItem &candidate, const LogicalItem &current)
{
    if (candidate.modificationTime.isValid() != current.modificationTime.isValid()) {
        return candidate.modificationTime.isValid();
    }
    if (candidate.modificationTime != current.modificationTime) {
        return candidate.modificationTime > current.modificationTime;
    }
    if (!candidate.id.isEmpty() && !current.id.isEmpty() && candidate.id != current.id) {
        return candidate.id < current.id;
    }
    return false;
}

QList<LogicalItem> matchingLogicalItems(const QList<LogicalItem> &items, const RemotePath &path)
{
    QList<LogicalItem> matches;
    for (const LogicalItem &item : items) {
        if (item.remote == path.remote && item.path == path.relativePath) {
            matches.append(item);
        }
    }
    return matches;
}

std::optional<LogicalItem> selectedLogicalItem(const QList<LogicalItem> &items, const RemotePath &path)
{
    std::optional<LogicalItem> selected;
    for (const LogicalItem &item : items) {
        if (item.remote != path.remote || item.path != path.relativePath) {
            continue;
        }
        if (!selected || preferLogicalItem(item, *selected)) {
            selected = item;
        }
    }
    return selected;
}

QJsonObject jsonItem(const RemotePath &path, const QFileInfo &info)
{
    const QString name = info.fileName().isEmpty() ? path.remote : info.fileName();
    return {
        {QStringLiteral("Path"), path.relativePath},
        {QStringLiteral("Name"), name},
        {QStringLiteral("Size"), info.isDir() ? -1 : info.size()},
        {QStringLiteral("MimeType"), info.isDir() ? QStringLiteral("inode/directory") : QMimeDatabase().mimeTypeForFile(info).name()},
        {QStringLiteral("ModTime"), info.lastModified().toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("IsDir"), info.isDir()},
    };
}

QJsonObject jsonItem(const LogicalItem &item)
{
    return {
        {QStringLiteral("Path"), item.path},
        {QStringLiteral("Name"), item.name},
        {QStringLiteral("ID"), item.id},
        {QStringLiteral("Size"), item.size},
        {QStringLiteral("MimeType"), item.mimeType},
        {QStringLiteral("ModTime"), item.modificationTime.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("IsDir"), item.isDirectory},
    };
}

int writePersistentStat(const QString &root, const QString &remoteSpec, const QList<LogicalItem> &logicalItems)
{
    const RemotePath path = resolveRemotePath(root, remoteSpec);
    if (!path.valid) {
        return writeError(QByteArrayLiteral("invalid remote path"));
    }

    if (const auto item = selectedLogicalItem(logicalItems, path)) {
        const QByteArray json = QJsonDocument(jsonItem(*item)).toJson(QJsonDocument::Compact);
        return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
    }

    const QFileInfo info(path.localPath);
    if (!info.exists()) {
        return writeError(QByteArrayLiteral("object not found"));
    }

    const QByteArray json = QJsonDocument(jsonItem(path, info)).toJson(QJsonDocument::Compact);
    return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
}

int writePersistentListing(const QString &root, const QString &remoteSpec, const QList<LogicalItem> &logicalItems)
{
    const RemotePath directoryPath = resolveRemotePath(root, remoteSpec);
    if (!directoryPath.valid) {
        return writeError(QByteArrayLiteral("invalid remote path"));
    }

    const QFileInfo directoryInfo(directoryPath.localPath);
    if (!directoryInfo.isDir()) {
        return writeError(QByteArrayLiteral("directory not found"));
    }

    QJsonArray items;
    const QFileInfoList entries = QDir(directoryPath.localPath).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &entry : entries) {
        RemotePath entryPath = directoryPath;
        entryPath.relativePath = directoryPath.relativePath.isEmpty() ? entry.fileName() : directoryPath.relativePath + QLatin1Char('/') + entry.fileName();
        entryPath.localPath = entry.absoluteFilePath();
        items.append(jsonItem(entryPath, entry));
    }
    for (const LogicalItem &item : logicalItems) {
        if (item.remote == directoryPath.remote && parentPath(item.path) == directoryPath.relativePath) {
            items.append(jsonItem(item));
        }
    }

    const QByteArray json = QJsonDocument(items).toJson(QJsonDocument::Compact);
    return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
}

QByteArray readStandardInput()
{
    QByteArray data;
    std::array<char, ChunkSize> chunk;
    for (;;) {
        const ssize_t received = ::read(STDIN_FILENO, chunk.data(), chunk.size());
        if (received > 0) {
            data.append(chunk.data(), received);
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        return received == 0 ? data : QByteArray();
    }
}

int streamLocalFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return writeError(QByteArrayLiteral("object not found"));
    }

    std::array<char, ChunkSize> chunk;
    for (;;) {
        const qint64 received = file.read(chunk.data(), chunk.size());
        if (received > 0) {
            if (!writeAll(STDOUT_FILENO, chunk.data(), received)) {
                return 1;
            }
            continue;
        }
        return received == 0 ? 0 : writeError(file.errorString().toUtf8());
    }
}

bool copyLocalFile(const QString &sourcePath, const QString &destinationPath)
{
    QFile source(sourcePath);
    QFile destination(destinationPath);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    std::array<char, ChunkSize> chunk;
    for (;;) {
        const qint64 received = source.read(chunk.data(), chunk.size());
        if (received > 0) {
            if (!writeAll(destination, chunk.data(), received)) {
                return false;
            }
            continue;
        }
        return received == 0 && destination.flush();
    }
}

int streamPersistentDownload(const QString &root, const QStringList &arguments, const QList<LogicalItem> &logicalItems)
{
    if (arguments.size() < 3) {
        return writeError(QByteArrayLiteral("missing remote path"));
    }

    QString remoteSpec = arguments.at(2);
    if (arguments.contains(QStringLiteral("--files-from-raw"))) {
        QByteArray requestedFile = readStandardInput();
        const qsizetype newline = requestedFile.indexOf('\n');
        if (newline >= 0) {
            requestedFile.truncate(newline);
        }
        if (requestedFile.isEmpty()) {
            return writeError(QByteArrayLiteral("missing requested file"));
        }
        if (!remoteSpec.endsWith(QLatin1Char(':')) && !remoteSpec.endsWith(QLatin1Char('/'))) {
            remoteSpec += QLatin1Char('/');
        }
        remoteSpec += QString::fromUtf8(requestedFile);
    }

    const RemotePath path = resolveRemotePath(root, remoteSpec);
    if (!path.valid) {
        return writeError(QByteArrayLiteral("invalid remote path"));
    }

    const QList<LogicalItem> matches = matchingLogicalItems(logicalItems, path);
    if (!matches.isEmpty()) {
        // Match rclone cat semantics for duplicate names: every matching
        // object is written to stdout. The worker must avoid this path when a
        // stable ID is available.
        for (const LogicalItem &item : matches) {
            if (const int result = streamLocalFile(item.sourcePath); result != 0) {
                return result;
            }
        }
        return 0;
    }
    return streamLocalFile(path.localPath);
}

int copyPersistentFile(const QString &root, const QStringList &arguments, const QList<LogicalItem> &logicalItems)
{
    if (arguments.size() < 4) {
        return writeError(QByteArrayLiteral("missing copy path"));
    }

    const RemotePath source = resolveRemotePath(root, arguments.at(2));
    const RemotePath destination = resolveRemotePath(root, arguments.at(3));
    QString sourcePath;
    QString destinationPath;
    if (source.valid) {
        sourcePath = source.localPath;
        if (const auto item = selectedLogicalItem(logicalItems, source)) {
            sourcePath = item->sourcePath;
        }
        destinationPath = destination.valid ? destination.localPath : arguments.at(3);
    } else if (destination.valid) {
        sourcePath = arguments.at(2);
        destinationPath = destination.localPath;
        QFile marker(QDir(root).filePath(QLatin1String(CopytoStartedFile)));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            marker.write(arguments.at(3).toUtf8());
            marker.close();
        }
        const QString block = QDir(root).filePath(QLatin1String(CopytoBlockFile));
        while (QFileInfo::exists(block)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (QFileInfo::exists(QDir(root).filePath(QLatin1String(CopytoFailFile)))) {
            return writeError(QByteArrayLiteral("copyto failed"));
        }
    } else {
        return writeError(QByteArrayLiteral("invalid copy path"));
    }
    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    const QString temporaryPath = destinationPath + QStringLiteral(".partial-test");
    QFile::remove(temporaryPath);
    if (!copyLocalFile(sourcePath, temporaryPath)
        || ::rename(QFile::encodeName(temporaryPath).constData(), QFile::encodeName(destinationPath).constData()) != 0) {
        QFile::remove(temporaryPath);
        return writeError(QByteArrayLiteral("copyto failed"));
    }
    return 0;
}

int copyPersistentFileById(const QStringList &arguments, const QList<LogicalItem> &logicalItems)
{
    if (arguments.size() < 6 || arguments.at(2) != QLatin1String("copyid")) {
        return writeError(QByteArrayLiteral("invalid backend command"));
    }

    const QString remoteSpec = arguments.at(3);
    const qsizetype separator = remoteSpec.indexOf(QLatin1Char(':'));
    const QString remote = separator > 0 ? remoteSpec.left(separator) : QString();
    const QString id = arguments.at(4);
    for (const LogicalItem &item : logicalItems) {
        if (item.remote == remote && item.id == id) {
            if (!copyLocalFile(item.sourcePath, arguments.at(5))) {
                return writeError(QByteArrayLiteral("copyid failed"));
            }
            return 0;
        }
    }
    return writeError(QByteArrayLiteral("object id not found"));
}

bool touchFile(const QString &path, const QByteArray &contents = {})
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

qint64 argumentSize(const QStringList &arguments)
{
    const qsizetype sizeOption = arguments.indexOf(QStringLiteral("--size"));
    if (sizeOption < 0 || sizeOption + 1 >= arguments.size()) {
        return -1;
    }

    bool ok = false;
    const qint64 size = arguments.at(sizeOption + 1).toLongLong(&ok);
    return ok ? size : -1;
}

int consumePersistentUpload(const QString &root, const QStringList &arguments)
{
    if (arguments.size() < 3) {
        return writeError(QByteArrayLiteral("missing remote path"));
    }

    const QString remoteSpec = arguments.at(2);
    const RemotePath path = resolveRemotePath(root, remoteSpec);
    if (!path.valid) {
        return writeError(QByteArrayLiteral("invalid remote path"));
    }
    if (!QDir().mkpath(QFileInfo(path.localPath).absolutePath())) {
        return writeError(QByteArrayLiteral("could not create destination directory"));
    }

    // Deliberately truncate and update the rcat destination in place. The
    // worker, rather than the fake backend, must provide atomic publication.
    QFile file(path.localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return writeError(file.errorString().toUtf8());
    }

    const QString blockPath = QDir(root).filePath(QLatin1String(RcatBlockFile));
    const QString failPath = QDir(root).filePath(QLatin1String(RcatFailFile));
    const QString startedPath = QDir(root).filePath(QLatin1String(RcatStartedFile));
    QFile::remove(startedPath);

    const qint64 totalSize = argumentSize(arguments);
    std::array<char, ChunkSize> chunk;
    qint64 transferred = 0;
    qint64 nextStats = 1024 * 1024;
    bool announced = false;
    for (;;) {
        const ssize_t received = ::read(STDIN_FILENO, chunk.data(), chunk.size());
        if (received > 0) {
            if (!writeAll(file, chunk.data(), received) || !file.flush()) {
                return writeError(file.errorString().toUtf8());
            }
            transferred += received;

            if (!announced) {
                if (!touchFile(startedPath, remoteSpec.toUtf8())) {
                    return writeError(QByteArrayLiteral("could not publish rcat test state"));
                }
                announced = true;
            }
            if (QFileInfo::exists(failPath)) {
                return writeError(QByteArrayLiteral("forced rcat failure"));
            }
            while (QFileInfo::exists(blockPath)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (transferred >= nextStats) {
                writeUploadStats(transferred, totalSize, QFileInfo(path.localPath).fileName());
                nextStats += 1024 * 1024;
            }
            continue;
        }
        return received == 0 ? 0 : writeError(QByteArrayLiteral("could not read upload data"));
    }
}

int movePersistentFile(const QString &root, const QStringList &arguments)
{
    if (arguments.size() < 4) {
        return writeError(QByteArrayLiteral("missing move path"));
    }

    const RemotePath source = resolveRemotePath(root, arguments.at(2));
    const RemotePath destination = resolveRemotePath(root, arguments.at(3));
    if (!source.valid || !destination.valid || source.remote != destination.remote) {
        return writeError(QByteArrayLiteral("invalid move path"));
    }
    if (QFileInfo(source.localPath).fileName().startsWith(QLatin1String(".kio-rclone-upload-")) && !arguments.contains(QStringLiteral("--ignore-times"))) {
        return writeError(QByteArrayLiteral("staged upload commit requires --ignore-times"));
    }
    if (!QFileInfo::exists(source.localPath)) {
        return writeError(QByteArrayLiteral("object not found"));
    }
    if (!QDir().mkpath(QFileInfo(destination.localPath).absolutePath())) {
        return writeError(QByteArrayLiteral("could not create destination directory"));
    }

    const QByteArray encodedSource = QFile::encodeName(source.localPath);
    const QByteArray encodedDestination = QFile::encodeName(destination.localPath);
    if (::rename(encodedSource.constData(), encodedDestination.constData()) != 0) {
        return writeError(QByteArrayLiteral("moveto failed: ") + QByteArray(std::strerror(errno)));
    }
    return 0;
}

int deletePersistentFile(const QString &root, const QStringList &arguments)
{
    if (arguments.size() < 3) {
        return writeError(QByteArrayLiteral("missing delete path"));
    }

    const RemotePath path = resolveRemotePath(root, arguments.at(2));
    if (!path.valid) {
        return writeError(QByteArrayLiteral("invalid delete path"));
    }
    if (!QFile::remove(path.localPath)) {
        return writeError(QFileInfo::exists(path.localPath) ? QByteArrayLiteral("deletefile failed") : QByteArrayLiteral("object not found"));
    }
    return 0;
}

int writePersistentRemotes(const QString &root)
{
    QJsonArray remotes;
    const QFileInfoList entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        remotes.append(QJsonObject{
            {QStringLiteral("name"), entry.fileName()},
            {QStringLiteral("type"), QStringLiteral("local")},
            {QStringLiteral("source"), QStringLiteral("file")},
        });
    }
    const QByteArray json = QJsonDocument(remotes).toJson(QJsonDocument::Compact);
    return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
}

int runPersistent(const QString &root, const QStringList &arguments)
{
    QString logicalItemsError;
    const QList<LogicalItem> logicalItems = readLogicalItems(&logicalItemsError);
    if (!logicalItemsError.isEmpty()) {
        return writeError(logicalItemsError.toUtf8());
    }

    const QString command = arguments.at(1);
    if (command == QLatin1String("lsjson")) {
        const QString remoteSpec = arguments.size() >= 3 ? arguments.at(2) : QString();
        return arguments.contains(QStringLiteral("--stat")) ? writePersistentStat(root, remoteSpec, logicalItems)
                                                            : writePersistentListing(root, remoteSpec, logicalItems);
    }
    if (command == QLatin1String("cat")) {
        return streamPersistentDownload(root, arguments, logicalItems);
    }
    if (command == QLatin1String("copyto")) {
        return copyPersistentFile(root, arguments, logicalItems);
    }
    if (command == QLatin1String("backend")) {
        return copyPersistentFileById(arguments, logicalItems);
    }
    if (command == QLatin1String("rcat")) {
        return consumePersistentUpload(root, arguments);
    }
    if (command == QLatin1String("moveto")) {
        return movePersistentFile(root, arguments);
    }
    if (command == QLatin1String("deletefile")) {
        return deletePersistentFile(root, arguments);
    }
    if (command == QLatin1String("listremotes")) {
        return writePersistentRemotes(root);
    }

    return writeError(QByteArrayLiteral("unsupported persistent fake command: ") + command.toUtf8());
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        return 1;
    }

    const QString remoteRoot = qEnvironmentVariable("KIO_RCLONE_TEST_REMOTE_ROOT");
    if (!remoteRoot.isEmpty()) {
        return runPersistent(remoteRoot, arguments);
    }

    const QString command = arguments.at(1);
    if (command == QLatin1String("lsjson")) {
        const QString remoteSpec = arguments.size() >= 3 ? arguments.at(2) : QString();
        if (arguments.contains(QStringLiteral("--stat"))) {
            return writeLegacyStat(remoteSpec);
        }
        return writeLegacyListing(remoteSpec);
    }
    if (command == QLatin1String("cat")) {
        return streamLegacyDownload();
    }
    if (command == QLatin1String("rcat")) {
        return consumeLegacyUpload();
    }
    if (command == QLatin1String("moveto") || command == QLatin1String("deletefile")) {
        return 0;
    }
    if (command == QLatin1String("listremotes")) {
        const QByteArray remotes(R"([{"name":"test","type":"drive","source":"file"}])");
        return writeAll(STDOUT_FILENO, remotes.constData(), remotes.size()) ? 0 : 1;
    }

    return writeError(QByteArrayLiteral("unsupported fake command: ") + command.toUtf8());
}
