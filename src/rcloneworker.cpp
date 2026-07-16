/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneworker.h"
#include "rcloneurl.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <sys/stat.h>

Q_LOGGING_CATEGORY(KIO_RCLONE, "kf.kio.workers.rclone")

namespace
{
constexpr qsizetype DownloadChunkSize = 64 * 1024;
constexpr qsizetype UploadChunkSize = 256 * 1024;
constexpr qsizetype MaximumDiagnosticSize = 64 * 1024;

void appendLimited(QByteArray &destination, const QByteArray &data)
{
    const qsizetype remaining = MaximumDiagnosticSize - destination.size();
    if (remaining > 0) {
        destination.append(data.left(remaining));
    }
}

QString fallbackMimeType(const RcloneItem &item)
{
    if (item.isDirectory) {
        return QStringLiteral("inode/directory");
    }
    if (!item.mimeType.isEmpty()) {
        return item.mimeType;
    }
    return QMimeDatabase().mimeTypeForFile(item.name, QMimeDatabase::MatchExtension).name();
}

QString fallbackMimeType(const QString &fileName)
{
    return QMimeDatabase().mimeTypeForFile(fileName, QMimeDatabase::MatchExtension).name();
}

class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.rclone" FILE "rclone.json")
};
} // namespace

extern "C" Q_DECL_EXPORT int kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_rclone"));

    if (argc != 4) {
        qCritical("Usage: kio_rclone protocol domain-socket1 domain-socket2");
        return 1;
    }

    RcloneWorker worker(argv[1], argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

RcloneWorker::RcloneWorker(const QByteArray &protocol, const QByteArray &poolSocket, const QByteArray &appSocket)
    : WorkerBase(protocol, poolSocket, appSocket)
{
}

KIO::WorkerResult RcloneWorker::listDir(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }
    if (rcloneUrl.isConfigureEntry()) {
        return launchConfiguration();
    }
    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    if (rcloneUrl.isRoot()) {
        QString error;
        const QStringList remotes = m_backend.remotes(&error, [this]() {
            return wasKilled();
        });
        if (!error.isEmpty()) {
            return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, url);
        }

        KIO::UDSEntryList entries;
        entries.reserve(remotes.size() + 2);
        entries.append(rootEntry());
        for (const QString &remote : remotes) {
            entries.append(remoteEntry(remote));
        }
        entries.append(configureEntry());
        listEntries(entries);
        return KIO::WorkerResult::pass();
    }

    QString error;
    const QList<RcloneItem> items = m_backend.list(rcloneUrl.remoteSpec(), &error, [this]() {
        return wasKilled();
    });
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!error.isEmpty()) {
        return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, url);
    }

    KIO::UDSEntryList entries;
    entries.reserve(items.size() + 1);
    entries.append(remoteEntry(rcloneUrl.remote(), true));
    for (const RcloneItem &item : items) {
        if (!item.name.isEmpty() && !item.name.contains(QLatin1Char('/'))) {
            entries.append(itemEntry(item));
        }
    }
    listEntries(entries);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::stat(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }
    if (rcloneUrl.isRoot()) {
        statEntry(rootEntry());
        return KIO::WorkerResult::pass();
    }
    if (rcloneUrl.isConfigureEntry()) {
        statEntry(configureEntry());
        return KIO::WorkerResult::pass();
    }
    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }
    if (rcloneUrl.isRemoteRoot()) {
        QString error;
        if (!remoteExists(rcloneUrl.remote(), &error)) {
            return error.isEmpty() ? KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString()) : errorResult(error, KIO::ERR_CANNOT_STAT, url);
        }
        statEntry(remoteEntry(rcloneUrl.remote()));
        return KIO::WorkerResult::pass();
    }

    QString error;
    const auto item = m_backend.stat(rcloneUrl.remoteSpec(), &error, [this]() {
        return wasKilled();
    });
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!item) {
        return errorResult(error, KIO::ERR_CANNOT_STAT, url);
    }
    statEntry(itemEntry(*item));
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mimetype(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid() || rcloneUrl.isRoot() || rcloneUrl.isRemoteRoot() || rcloneUrl.isConfigureEntry()) {
        mimeType(QStringLiteral("inode/directory"));
        return KIO::WorkerResult::pass();
    }

    QString error;
    const auto item = m_backend.stat(rcloneUrl.remoteSpec(), &error, [this]() {
        return wasKilled();
    });
    if (!item) {
        return errorResult(error, KIO::ERR_DOES_NOT_EXIST, url);
    }
    mimeType(fallbackMimeType(*item));
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::get(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid() || rcloneUrl.remoteSpec().isEmpty() || rcloneUrl.isRemoteRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
    }
    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    // CopyJob has already obtained the source size before starting its
    // get/put data pump. Repeating lsjson --stat here starts a second rclone
    // process and resolves the complete remote path twice for every download.
    mimeType(fallbackMimeType(rcloneUrl.remotePath()));

    const QString fileName = rcloneUrl.remotePath().section(QLatin1Char('/'), -1);
    if (fileName.contains(QLatin1Char('\n'))) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }
    const QString parentPath = rcloneUrl.remotePath().section(QLatin1Char('/'), 0, -2);
    const QString parentSpec = rcloneUrl.remote() + QLatin1Char(':') + parentPath;

    QProcess process;
    configureProcess(process,
                     m_backend.executable(),
                     {
                         QStringLiteral("cat"),
                         parentSpec,
                         QStringLiteral("--files-from-raw"),
                         QStringLiteral("-"),
                         QStringLiteral("--buffer-size"),
                         QStringLiteral("0"),
                         QStringLiteral("--multi-thread-streams"),
                         QStringLiteral("0"),
                         QStringLiteral("--log-level"),
                         QStringLiteral("ERROR"),
                     });
    process.setReadChannel(QProcess::StandardOutput);
    process.start();
    if (!process.waitForStarted(5000)) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS, process.errorString());
    }
    const QByteArray requestedFile = fileName.toUtf8() + '\n';
    if (process.write(requestedFile) != requestedFile.size() || !process.waitForBytesWritten(5000)) {
        stopProcess(process);
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
    }
    process.closeWriteChannel();

    QByteArray standardError;
    qint64 processed = 0;
    while (process.state() != QProcess::NotRunning || process.bytesAvailable() > 0) {
        if (wasKilled()) {
            stopProcess(process);
            return KIO::WorkerResult::pass();
        }

        if (process.bytesAvailable() == 0) {
            process.waitForReadyRead(100);
        }
        appendLimited(standardError, process.readAllStandardError());

        while (process.bytesAvailable() > 0) {
            const QByteArray chunk = process.read(qMin<qint64>(DownloadChunkSize, process.bytesAvailable()));
            if (chunk.isEmpty()) {
                break;
            }
            data(chunk);
            processed += chunk.size();
            processedSize(processed);
        }
    }
    appendLimited(standardError, process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return errorResult(QString::fromUtf8(standardError).trimmed(), KIO::ERR_CANNOT_READ, url);
    }

    data(QByteArray());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::put(const QUrl &url, int permissions, KIO::JobFlags flags)
{
    Q_UNUSED(permissions)

    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid() || rcloneUrl.remoteSpec().isEmpty() || rcloneUrl.isRemoteRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, url.toDisplayString());
    }
    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    if (!(flags & KIO::Overwrite)) {
        std::optional<RcloneItem> existingItem;
        QString error;
        if (destinationExists(rcloneUrl.remoteSpec(), &existingItem, &error)) {
            return KIO::WorkerResult::fail(existingItem && existingItem->isDirectory ? KIO::ERR_DIR_ALREADY_EXIST : KIO::ERR_FILE_ALREADY_EXIST,
                                           url.toDisplayString());
        }
        if (!error.isEmpty() && !RcloneBackend::isNotFoundError(error)) {
            return errorResult(error, KIO::ERR_CANNOT_STAT, url);
        }
    }

    bool sizeOk = false;
    const qint64 sourceSize = metaData(QStringLiteral("sourceSize")).toLongLong(&sizeOk);
    QStringList arguments{
        QStringLiteral("rcat"),
        rcloneUrl.remoteSpec(),
        QStringLiteral("--buffer-size"),
        QStringLiteral("0"),
        QStringLiteral("--log-level"),
        QStringLiteral("ERROR"),
    };
    if (sizeOk && sourceSize >= 0) {
        arguments.append({QStringLiteral("--size"), QString::number(sourceSize)});
        totalSize(sourceSize);
    }

    QProcess process;
    configureProcess(process, m_backend.executable(), arguments);
    process.start();
    if (!process.waitForStarted(5000)) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS, process.errorString());
    }

    QByteArray standardOutput;
    QByteArray standardError;
    qint64 processed = 0;
    while (process.state() != QProcess::NotRunning) {
        if (wasKilled()) {
            stopProcess(process);
            return KIO::WorkerResult::pass();
        }

        dataReq();
        QByteArray sourceData;
        const int readResult = readData(sourceData);
        if (readResult < 0) {
            stopProcess(process);
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
        }
        if (readResult == 0) {
            break;
        }

        qsizetype offset = 0;
        while (offset < sourceData.size()) {
            if (process.state() == QProcess::NotRunning) {
                collectProcessOutput(process, standardOutput, standardError);
                return errorResult(QString::fromUtf8(standardError).trimmed(), KIO::ERR_CANNOT_WRITE, url);
            }

            const qsizetype amount = qMin(UploadChunkSize, sourceData.size() - offset);
            const qint64 accepted = process.write(sourceData.constData() + offset, amount);
            if (accepted <= 0) {
                stopProcess(process);
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, url.toDisplayString());
            }
            offset += accepted;

            while (process.bytesToWrite() > 0 && process.state() != QProcess::NotRunning) {
                if (wasKilled()) {
                    stopProcess(process);
                    return KIO::WorkerResult::pass();
                }
                process.waitForBytesWritten(100);
                collectProcessOutput(process, standardOutput, standardError);
            }
        }

        processed += sourceData.size();
        processedSize(processed);
    }

    process.closeWriteChannel();
    infoMessage(i18n("Finalizing upload…"));
    while (process.state() != QProcess::NotRunning) {
        if (wasKilled()) {
            stopProcess(process);
            return KIO::WorkerResult::pass();
        }
        process.waitForFinished(100);
        collectProcessOutput(process, standardOutput, standardError);
    }
    collectProcessOutput(process, standardOutput, standardError);

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return errorResult(QString::fromUtf8(standardError).trimmed(), KIO::ERR_CANNOT_WRITE, url);
    }
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url, int permissions)
{
    Q_UNUSED(permissions)

    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid() || rcloneUrl.remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_MKDIR, url.toDisplayString());
    }

    std::optional<RcloneItem> existingItem;
    QString error;
    if (destinationExists(rcloneUrl.remoteSpec(), &existingItem, &error)) {
        return KIO::WorkerResult::fail(existingItem && existingItem->isDirectory ? KIO::ERR_DIR_ALREADY_EXIST : KIO::ERR_FILE_ALREADY_EXIST,
                                       url.toDisplayString());
    }
    if (!error.isEmpty() && !RcloneBackend::isNotFoundError(error)) {
        return errorResult(error, KIO::ERR_CANNOT_STAT, url);
    }

    return commandResult(runCommand({QStringLiteral("mkdir"), rcloneUrl.remoteSpec()}), KIO::ERR_CANNOT_MKDIR, url);
}

KIO::WorkerResult RcloneWorker::rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags)
{
    const RcloneUrl source(src);
    const RcloneUrl destination(dest);
    if (!source.isValid() || !destination.isValid() || source.remoteSpec().isEmpty() || destination.remoteSpec().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME, src.toDisplayString());
    }
    if (source.remoteSpec() == destination.remoteSpec()) {
        return KIO::WorkerResult::fail(KIO::ERR_IDENTICAL_FILES, src.toDisplayString());
    }
    if (source.remote() != destination.remote()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Moving between different rclone remotes is streamed by KIO."));
    }

    if (!(flags & KIO::Overwrite)) {
        std::optional<RcloneItem> existingItem;
        QString error;
        if (destinationExists(destination.remoteSpec(), &existingItem, &error)) {
            return KIO::WorkerResult::fail(existingItem && existingItem->isDirectory ? KIO::ERR_DIR_ALREADY_EXIST : KIO::ERR_FILE_ALREADY_EXIST,
                                           dest.toDisplayString());
        }
        if (!error.isEmpty() && !RcloneBackend::isNotFoundError(error)) {
            return errorResult(error, KIO::ERR_CANNOT_STAT, dest);
        }
    }

    return commandResult(runCommand({QStringLiteral("moveto"), source.remoteSpec(), destination.remoteSpec()}), KIO::ERR_CANNOT_RENAME, src);
}

KIO::WorkerResult RcloneWorker::copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags)
{
    Q_UNUSED(src)
    Q_UNUSED(dest)
    Q_UNUSED(permissions)
    Q_UNUSED(flags)

    // KIO's get + put fallback preserves pause/cancel backpressure. A direct
    // rclone copy process would continue independently while Dolphin is paused.
    return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION);
}

KIO::WorkerResult RcloneWorker::del(const QUrl &url, bool isFile)
{
    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid() || rcloneUrl.remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_DELETE, url.toDisplayString());
    }

    QString command;
    if (isFile) {
        command = QStringLiteral("deletefile");
    } else if (metaData(QStringLiteral("recurse")) == QLatin1String("true")) {
        command = QStringLiteral("purge");
    } else {
        command = QStringLiteral("rmdir");
    }

    return commandResult(runCommand({command, rcloneUrl.remoteSpec()}), isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR, url);
}

KIO::WorkerResult RcloneWorker::fileSystemFreeSpace(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);
    if (!rcloneUrl.isValid() || rcloneUrl.remote().isEmpty() || rcloneUrl.isConfigureEntry()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_STAT, url.toDisplayString());
    }

    QString error;
    const auto space = m_backend.about(rcloneUrl.remote() + QLatin1Char(':'), &error, [this]() {
        return wasKilled();
    });
    if (!space) {
        return errorResult(error, KIO::ERR_UNSUPPORTED_ACTION, url);
    }
    if (space->total >= 0) {
        setMetaData(QStringLiteral("total"), QString::number(space->total));
    }
    if (space->free >= 0) {
        setMetaData(QStringLiteral("available"), QString::number(space->free));
    }
    return KIO::WorkerResult::pass();
}

KIO::UDSEntry RcloneWorker::rootEntry() const
{
    KIO::UDSEntry entry;
    entry.reserve(6);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Rclone Remotes"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-cloud"));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry RcloneWorker::configureEntry() const
{
    KIO::UDSEntry entry;
    entry.reserve(7);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, RcloneUrl::ConfigureEntry);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Configure Remotes…"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IXUSR);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("configure"));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    entry.fastInsert(KIO::UDSEntry::UDS_HIDDEN, 0);
    return entry;
}

KIO::UDSEntry RcloneWorker::remoteEntry(const QString &name, bool currentDirectory) const
{
    KIO::UDSEntry entry;
    entry.reserve(6);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, currentDirectory ? QStringLiteral(".") : name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, currentDirectory ? name : name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-cloud"));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry RcloneWorker::itemEntry(const RcloneItem &item) const
{
    KIO::UDSEntry entry;
    entry.reserve(7);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, item.name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, item.isDirectory ? S_IFDIR : S_IFREG);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS,
                     item.isDirectory ? S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    entry.fastInsert(KIO::UDSEntry::UDS_SIZE, qMax<qint64>(0, item.size));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, fallbackMimeType(item));
    if (item.isDirectory) {
        entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder"));
    }
    if (item.modificationTime.isValid()) {
        entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, item.modificationTime.toSecsSinceEpoch());
    }
    return entry;
}

KIO::WorkerResult RcloneWorker::ensureBackend() const
{
    if (m_backend.isAvailable()) {
        return KIO::WorkerResult::pass();
    }
    return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS,
                                   i18n("rclone was not found. Install it or set KIO_RCLONE_EXECUTABLE to "
                                        "its full path."));
}

KIO::WorkerResult RcloneWorker::launchConfiguration()
{
    QString executable = QStandardPaths::findExecutable(QStringLiteral("kio-rclone-config"));
    if (executable.isEmpty()) {
        const QString localExecutable = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.local/bin/kio-rclone-config");
        if (QFileInfo(localExecutable).isExecutable()) {
            executable = localExecutable;
        }
    }
    if (executable.isEmpty() || !QProcess::startDetached(executable, {})) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS, i18n("The KIO Rclone configuration utility could not be started."));
    }

    redirection(RcloneUrl::rootUrl());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::commandResult(const RcloneResult &result, int fallbackError, const QUrl &url) const
{
    if (result.cancelled || wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (result.success()) {
        return KIO::WorkerResult::pass();
    }
    if (!result.started) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS, result.errorMessage());
    }
    return errorResult(result.errorMessage(), fallbackError, url);
}

KIO::WorkerResult RcloneWorker::errorResult(const QString &message, int fallbackError, const QUrl &url) const
{
    const QString lowered = message.toLower();
    if (RcloneBackend::isNotFoundError(message)) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }
    if (lowered.contains(QStringLiteral("permission denied")) || lowered.contains(QStringLiteral("access denied"))
        || lowered.contains(QStringLiteral("unauthorized")) || lowered.contains(QStringLiteral("invalid_grant"))) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, url.toDisplayString());
    }
    if (lowered.contains(QStringLiteral("didn't find section in config file")) || lowered.contains(QStringLiteral("config file not found"))) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
    }

    const QString usefulMessage = message.isEmpty() ? url.toDisplayString() : message;
    return KIO::WorkerResult::fail(fallbackError, usefulMessage);
}

RcloneResult RcloneWorker::runCommand(const QStringList &arguments, int timeoutMs) const
{
    return m_backend.run(arguments, timeoutMs, [this]() {
        return wasKilled();
    });
}

bool RcloneWorker::remoteExists(const QString &remote, QString *error) const
{
    return m_backend
        .remotes(error,
                 [this]() {
                     return wasKilled();
                 })
        .contains(remote);
}

bool RcloneWorker::destinationExists(const QString &remoteSpec, std::optional<RcloneItem> *item, QString *error) const
{
    const auto result = m_backend.stat(remoteSpec, error, [this]() {
        return wasKilled();
    });
    if (item) {
        *item = result;
    }
    return result.has_value();
}

void RcloneWorker::configureProcess(QProcess &process, const QString &program, const QStringList &arguments)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
}

void RcloneWorker::stopProcess(QProcess &process)
{
    if (process.state() == QProcess::NotRunning) {
        return;
    }
    process.terminate();
    if (!process.waitForFinished(1000)) {
        process.kill();
        process.waitForFinished(1000);
    }
}

void RcloneWorker::collectProcessOutput(QProcess &process, QByteArray &standardOutput, QByteArray &standardError)
{
    appendLimited(standardOutput, process.readAllStandardOutput());
    appendLimited(standardError, process.readAllStandardError());
}

#include "rcloneworker.moc"
