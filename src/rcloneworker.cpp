/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneworker.h"
#include "rcloneentryformat.h"
#include "rcloneprocess.h"
#include "rcloneurl.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>

#include <limits>
#include <optional>
#include <sys/stat.h>
#include <utility>

Q_LOGGING_CATEGORY(KIO_RCLONE, "kf.kio.workers.rclone")

namespace
{
constexpr qsizetype DownloadChunkSize = 64 * 1024;

std::optional<QUrl> configurationLauncherUrl()
{
    const QString desktopFile = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, QStringLiteral("org.kde.kio-rclone-config.desktop"));

    if (desktopFile.isEmpty()) {
        return std::nullopt;
    }

    return QUrl::fromLocalFile(desktopFile);
}

KIO::WorkerResult configurationEntryMutationError(int error)
{
    return KIO::WorkerResult::fail(error, i18n("“Configure Remotes…” is a virtual launcher and cannot be modified."));
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
        return KIO::WorkerResult::fail(KIO::ERR_IS_FILE, url.toDisplayString());
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

        // Resolve every remote's provider type in a single call so the listing
        // can show a matching icon (Drive, Dropbox…). Cosmetic: a failure here
        // simply falls back to the generic cloud folder.
        const QHash<QString, QString> types = m_backend.remoteTypes(nullptr, [this]() {
            return wasKilled();
        });

        KIO::UDSEntryList entries;
        entries.reserve(remotes.size() + 2);
        entries.append(rootEntry());
        for (const QString &remote : remotes) {
            entries.append(remoteEntry(remote, false, types.value(remote)));
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

    QList<RcloneItem> visibleItems;
    QHash<QString, qsizetype> itemIndexes;
    visibleItems.reserve(items.size());
    for (const RcloneItem &item : items) {
        if (item.name.isEmpty() || item.name.contains(QLatin1Char('/'))) {
            continue;
        }

        const auto existing = itemIndexes.constFind(item.name);
        if (existing == itemIndexes.cend()) {
            itemIndexes.insert(item.name, visibleItems.size());
            visibleItems.append(item);
            continue;
        }

        RcloneItem &selected = visibleItems[*existing];
        if (RcloneEntryFormat::preferItem(item, selected)) {
            selected = item;
        }
        selected.ambiguous = true;
        selected.readOnly = true;
    }

    KIO::UDSEntryList entries;
    entries.reserve(visibleItems.size() + 1);
    entries.append(remoteEntry(rcloneUrl.remote(), true));
    for (const RcloneItem &item : std::as_const(visibleItems)) {
        entries.append(itemEntry(item));
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
        // A single config dump both confirms the remote exists and yields its
        // provider type, so the stat entry can carry the matching icon.
        QString error;
        const QHash<QString, QString> types = m_backend.remoteTypes(&error, [this]() {
            return wasKilled();
        });
        if (!error.isEmpty()) {
            return errorResult(error, KIO::ERR_CANNOT_STAT, url);
        }
        if (!types.contains(rcloneUrl.remote())) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        statEntry(remoteEntry(rcloneUrl.remote(), false, types.value(rcloneUrl.remote())));
        return KIO::WorkerResult::pass();
    }

    QString error;
    const auto item = sourceItem(rcloneUrl, &error);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!item) {
        return errorResult(error, KIO::ERR_CANNOT_STAT, url);
    }

    RcloneItem resolvedItem = *item;
    if (!resolvedItem.isDirectory && resolvedItem.size < 0) {
        if (const auto result = resolveUnknownSize(rcloneUrl, resolvedItem); !result.success()) {
            return result;
        }
        if (wasKilled()) {
            return KIO::WorkerResult::pass();
        }
    }
    statEntry(itemEntry(resolvedItem));
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mimetype(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);

    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }

    if (rcloneUrl.isConfigureEntry()) {
        mimeType(QStringLiteral("application/x-desktop"));
        return KIO::WorkerResult::pass();
    }

    if (rcloneUrl.isRoot() || rcloneUrl.isRemoteRoot()) {
        mimeType(QStringLiteral("inode/directory"));
        return KIO::WorkerResult::pass();
    }

    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    QString error;
    const auto item = sourceItem(rcloneUrl, &error);

    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }

    if (!item) {
        return errorResult(error, KIO::ERR_DOES_NOT_EXIST, url);
    }

    mimeType(RcloneEntryFormat::fallbackMimeType(*item));
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::get(const QUrl &url)
{
    const RcloneUrl rcloneUrl(url);

    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }

    if (rcloneUrl.isConfigureEntry()) {
        const auto launcher = configurationLauncherUrl();

        if (!launcher) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, i18n("The KIO Rclone configuration launcher is not installed."));
        }

        redirection(*launcher);
        return KIO::WorkerResult::pass();
    }

    if (rcloneUrl.remoteSpec().isEmpty() || rcloneUrl.isRemoteRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
    }

    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    QString error;
    auto item = sourceItem(rcloneUrl, &error);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!item) {
        return errorResult(error, KIO::ERR_CANNOT_READ, url);
    }

    mimeType(RcloneEntryFormat::fallbackMimeType(*item));
    if (cachedDownloadMatches(rcloneUrl, *item)) {
        return sendCachedDownload(rcloneUrl);
    }
    if (item->ambiguous || item->size < 0) {
        if (const auto result = cacheRemoteFile(rcloneUrl, *item); !result.success()) {
            return result;
        }
        if (wasKilled()) {
            return KIO::WorkerResult::pass();
        }
        return sendCachedDownload(rcloneUrl);
    }
    totalSize(item->size);

    const QString fileName = rcloneUrl.remotePath().section(QLatin1Char('/'), -1);
    if (fileName.contains(QLatin1Char('\n'))) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }
    const QString parentPath = rcloneUrl.remotePath().section(QLatin1Char('/'), 0, -2);
    const QString parentSpec = rcloneUrl.remote() + QLatin1Char(':') + parentPath;

    QProcess process;
    RcloneProcess::configureProcess(process,
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
        RcloneProcess::stopProcess(process);
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
    }
    process.closeWriteChannel();

    QByteArray standardError;
    qint64 processed = 0;
    while (process.state() != QProcess::NotRunning || process.bytesAvailable() > 0) {
        if (wasKilled()) {
            RcloneProcess::stopProcess(process);
            return KIO::WorkerResult::pass();
        }

        if (process.bytesAvailable() == 0) {
            process.waitForReadyRead(100);
        }
        RcloneProcess::appendLimited(standardError, process.readAllStandardError());

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
    RcloneProcess::appendLimited(standardError, process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return errorResult(QString::fromUtf8(standardError).trimmed(), KIO::ERR_CANNOT_READ, url);
    }
    if (item->size >= 0 && processed != item->size) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, i18n("The remote file changed while it was being read."));
    }

    data(QByteArray());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::put(const QUrl &url, int permissions, KIO::JobFlags flags)
{
    Q_UNUSED(permissions)

    const RcloneUrl rcloneUrl(url);

    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }

    if (rcloneUrl.isConfigureEntry()) {
        return configurationEntryMutationError(KIO::ERR_WRITE_ACCESS_DENIED);
    }

    if (rcloneUrl.remoteSpec().isEmpty() || rcloneUrl.isRemoteRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, url.toDisplayString());
    }

    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }
    if (m_cachedDownloadSpec == rcloneUrl.remoteSpec()) {
        clearCachedDownload();
    }

    QString destinationError;
    const std::optional<RcloneItem> expectedDestination = sourceItem(rcloneUrl, &destinationError);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!expectedDestination && !destinationError.isEmpty() && !RcloneBackend::isNotFoundError(destinationError)) {
        return errorResult(destinationError, KIO::ERR_CANNOT_STAT, url);
    }
    if (expectedDestination) {
        if (expectedDestination->isDirectory) {
            return KIO::WorkerResult::fail(KIO::ERR_DIR_ALREADY_EXIST, url.toDisplayString());
        }
        if (expectedDestination->ambiguous) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE,
                                           i18n("Multiple remote objects have this name. Rename or remove the duplicates before editing this file."));
        }
        if (expectedDestination->readOnly) {
            return KIO::WorkerResult::fail(
                KIO::ERR_WRITE_ACCESS_DENIED,
                i18n("This is an exported cloud document with no stable remote size. It is available read-only to avoid replacing the original."));
        }
        if (!(flags & KIO::Overwrite)) {
            return KIO::WorkerResult::fail(KIO::ERR_FILE_ALREADY_EXIST, url.toDisplayString());
        }
    }

    bool sizeOk = false;
    const qint64 sourceSize = metaData(QStringLiteral("sourceSize")).toLongLong(&sizeOk);
    if (sizeOk && sourceSize >= 0) {
        totalSize(sourceSize);
    }

    const QString suffix = QFileInfo(rcloneUrl.remotePath()).completeSuffix();
    const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    QTemporaryFile localUpload(QDir::tempPath() + QStringLiteral("/kio-rclone-upload-XXXXXX") + extension);
    if (!localUpload.open()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_OPEN_FOR_WRITING, localUpload.errorString());
    }

    qint64 processed = 0;
    infoMessage(i18n("Preparing upload…"));
    for (;;) {
        if (wasKilled()) {
            return KIO::WorkerResult::pass();
        }

        dataReq();
        QByteArray sourceData;
        const int readResult = readData(sourceData);
        if (readResult < 0) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
        }
        if (readResult == 0) {
            break;
        }
        if (sizeOk && sourceSize >= 0 && processed > sourceSize - sourceData.size()) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("The upload source changed size while it was being read."));
        }
        if (localUpload.write(sourceData) != sourceData.size()) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, localUpload.errorString());
        }
        processed += sourceData.size();
    }

    if (sizeOk && sourceSize >= 0 && processed != sourceSize) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("The upload source changed size while it was being read."));
    }
    if (!localUpload.flush()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, localUpload.errorString());
    }
    const QString localUploadPath = localUpload.fileName();
    localUpload.close();

    const auto destinationChanged = [&expectedDestination](const std::optional<RcloneItem> &current) {
        return expectedDestination.has_value() != current.has_value()
            || (expectedDestination && current && (current->ambiguous || RcloneEntryFormat::itemVersion(*expectedDestination) != RcloneEntryFormat::itemVersion(*current)));
    };
    const auto currentDestination = [this, &rcloneUrl](QString *error) {
        return sourceItem(rcloneUrl, error);
    };

    QString currentError;
    std::optional<RcloneItem> current = currentDestination(&currentError);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!current && !currentError.isEmpty() && !RcloneBackend::isNotFoundError(currentError)) {
        return errorResult(currentError, KIO::ERR_CANNOT_STAT, url);
    }
    if (destinationChanged(current)) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("The remote file changed while the local edit was being prepared."));
    }

    const RcloneResult upload = runUpload(localUploadPath, rcloneUrl.remoteSpec());
    if (!upload.success()) {
        return commandResult(upload, KIO::ERR_CANNOT_WRITE, url);
    }

    processedSize(sizeOk && sourceSize >= 0 ? sourceSize : processed);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url, int permissions)
{
    Q_UNUSED(permissions)

    const RcloneUrl rcloneUrl(url);

    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }

    if (rcloneUrl.isConfigureEntry()) {
        return configurationEntryMutationError(KIO::ERR_FILE_ALREADY_EXIST);
    }

    if (rcloneUrl.remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_MKDIR, url.toDisplayString());
    }

    if (const auto result = ensureBackend(); !result.success()) {
        return result;
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
    clearCachedDownload();

    const RcloneUrl source(src);
    const RcloneUrl destination(dest);

    if (!source.isValid() || !destination.isValid()) {
        const QUrl &invalidUrl = !source.isValid() ? src : dest;
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, invalidUrl.toDisplayString());
    }
    if (source.isConfigureEntry() || destination.isConfigureEntry()) {
        return configurationEntryMutationError(KIO::ERR_CANNOT_RENAME);
    }
    if (source.remoteSpec().isEmpty() || destination.remoteSpec().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME, src.toDisplayString());
    }
    if (source.remoteSpec() == destination.remoteSpec()) {
        return KIO::WorkerResult::fail(KIO::ERR_IDENTICAL_FILES, src.toDisplayString());
    }
    if (source.remote() != destination.remote()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Moving between different rclone remotes is streamed by KIO."));
    }
    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    QString sourceError;
    const std::optional<RcloneItem> sourceItemResult = sourceItem(source, &sourceError);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!sourceItemResult) {
        return errorResult(sourceError, KIO::ERR_CANNOT_RENAME, src);
    }
    if (sourceItemResult->ambiguous) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME,
                                       i18n("Multiple remote objects have the source name. Resolve the duplicates before renaming this path."));
    }

    QString destinationError;
    const std::optional<RcloneItem> destinationItem = sourceItem(destination, &destinationError);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!destinationItem && !destinationError.isEmpty() && !RcloneBackend::isNotFoundError(destinationError)) {
        return errorResult(destinationError, KIO::ERR_CANNOT_STAT, dest);
    }
    if (destinationItem) {
        if (destinationItem->ambiguous || destinationItem->readOnly) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME, i18n("The destination is ambiguous or read-only and cannot be replaced safely."));
        }
        if (!(flags & KIO::Overwrite)) {
            return KIO::WorkerResult::fail(destinationItem->isDirectory ? KIO::ERR_DIR_ALREADY_EXIST : KIO::ERR_FILE_ALREADY_EXIST, dest.toDisplayString());
        }
    }

    return commandResult(runCommand({QStringLiteral("moveto"), source.remoteSpec(), destination.remoteSpec(), QStringLiteral("--ignore-times")}),
                         KIO::ERR_CANNOT_RENAME,
                         src);
}

KIO::WorkerResult RcloneWorker::copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags)
{
    Q_UNUSED(permissions)
    Q_UNUSED(flags)

    const RcloneUrl source(src);
    const RcloneUrl destination(dest);

    if (!source.isValid() || !destination.isValid()) {
        const QUrl &invalidUrl = !source.isValid() ? src : dest;
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, invalidUrl.toDisplayString());
    }

    if (source.isConfigureEntry()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, i18n("“Configure Remotes…” is a virtual launcher and cannot be copied."));
    }

    if (destination.isConfigureEntry()) {
        return configurationEntryMutationError(KIO::ERR_WRITE_ACCESS_DENIED);
    }

    // KIO's get + put fallback preserves pause/cancel backpressure. A direct
    // rclone copy process would continue independently while Dolphin is paused.
    return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION);
}

KIO::WorkerResult RcloneWorker::del(const QUrl &url, bool isFile)
{
    clearCachedDownload();

    const RcloneUrl rcloneUrl(url);

    if (!rcloneUrl.isValid()) {
        return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
    }
    if (rcloneUrl.isConfigureEntry()) {
        return configurationEntryMutationError(KIO::ERR_CANNOT_DELETE);
    }
    if (rcloneUrl.remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_DELETE, url.toDisplayString());
    }

    if (const auto result = ensureBackend(); !result.success()) {
        return result;
    }

    QString sourceError;
    const std::optional<RcloneItem> item = sourceItem(rcloneUrl, &sourceError);
    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!item) {
        return errorResult(sourceError, isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR, url);
    }
    if (item->ambiguous) {
        return KIO::WorkerResult::fail(isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR,
                                       i18n("Multiple remote objects have this name. Resolve the duplicates before deleting this path."));
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
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-kio-rclone"));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry RcloneWorker::configureEntry() const
{
    KIO::UDSEntry entry;
    entry.reserve(8);

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, RcloneUrl::ConfigureEntry);

    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Configure Remotes…"));

    // Not a directory: it cannot be entered or listed.
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);

    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH);

    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("configure"));

    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("application/x-desktop"));

    if (const auto launcher = configurationLauncherUrl()) {
        entry.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, launcher->toString());
    }

    entry.fastInsert(KIO::UDSEntry::UDS_HIDDEN, 0);

    return entry;
}

KIO::UDSEntry RcloneWorker::remoteEntry(const QString &name, bool currentDirectory, const QString &type) const
{
    KIO::UDSEntry entry;
    entry.reserve(6);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, currentDirectory ? QStringLiteral(".") : name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, currentDirectory ? name : name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, RcloneEntryFormat::iconForRemoteType(type));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry RcloneWorker::itemEntry(const RcloneItem &item) const
{
    KIO::UDSEntry entry;
    entry.reserve(7);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, item.name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, item.isDirectory ? S_IFDIR : S_IFREG);
    const mode_t fileAccess = item.readOnly ? S_IRUSR | S_IRGRP | S_IROTH : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, item.isDirectory ? S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH : fileAccess);
    if (item.size >= 0) {
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, item.size);
    }
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, RcloneEntryFormat::fallbackMimeType(item));
    if (item.ambiguous) {
        entry.fastInsert(KIO::UDSEntry::UDS_COMMENT, i18n("Multiple remote objects have this name. KIO Rclone is showing the newest one."));
    }
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

RcloneResult RcloneWorker::runUpload(const QString &localPath, const QString &remoteSpec)
{
    RcloneResult result;
    QProcess process;
    RcloneProcess::configureProcess(process,
                     m_backend.executable(),
                     {
                         QStringLiteral("copyto"),
                         localPath,
                         remoteSpec,
                         QStringLiteral("--ignore-times"),
                         QStringLiteral("--stats"),
                         QStringLiteral("500ms"),
                         QStringLiteral("--stats-one-line"),
                         QStringLiteral("--stats-log-level"),
                         QStringLiteral("NOTICE"),
                         QStringLiteral("--use-json-log"),
                         QStringLiteral("--log-level"),
                         QStringLiteral("NOTICE"),
                     });
    process.start();
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        return result;
    }

    QByteArray pendingLog;
    qint64 remoteProcessed = 0;
    const auto handleLogLine = [this, &result, &remoteProcessed](const QByteArray &line) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            RcloneProcess::appendLimited(result.standardError, line + '\n');
            return;
        }

        const QJsonObject object = document.object();
        const QJsonObject stats = object.value(QStringLiteral("stats")).toObject();
        if (!stats.isEmpty()) {
            qint64 transferred = stats.value(QStringLiteral("bytes")).toVariant().toLongLong();
            double transferSpeed = stats.value(QStringLiteral("speed")).toDouble();
            int percentage = -1;
            qint64 eta = -1;

            const QJsonArray transferring = stats.value(QStringLiteral("transferring")).toArray();
            if (!transferring.isEmpty()) {
                const QJsonObject transfer = transferring.at(0).toObject();
                transferred = transfer.value(QStringLiteral("bytes")).toVariant().toLongLong();
                transferSpeed = transfer.value(QStringLiteral("speedAvg")).toDouble();
                if (transferSpeed <= 0) {
                    transferSpeed = transfer.value(QStringLiteral("speed")).toDouble();
                }
                percentage = transfer.value(QStringLiteral("percentage")).toInt(-1);
                const QJsonValue etaValue = transfer.value(QStringLiteral("eta"));
                if (etaValue.isDouble()) {
                    eta = qRound64(etaValue.toDouble());
                }
            } else {
                const qint64 total = stats.value(QStringLiteral("totalBytes")).toVariant().toLongLong();
                if (total > 0) {
                    percentage = qBound(0, int((transferred * 100) / total), 100);
                }
                const QJsonValue etaValue = stats.value(QStringLiteral("eta"));
                if (etaValue.isDouble()) {
                    eta = qRound64(etaValue.toDouble());
                }
            }

            remoteProcessed = qMax(remoteProcessed, transferred);
            processedSize(remoteProcessed);
            if (transferSpeed > 0) {
                speed(static_cast<unsigned long>(qMin(transferSpeed, double(std::numeric_limits<unsigned long>::max()))));
            }

            const QString formattedSpeed = QLocale().formattedDataSize(qMax<qint64>(0, qRound64(transferSpeed)));
            if (percentage >= 0 && eta >= 0) {
                infoMessage(i18n("Uploading… %1% · %2/s · %3 s remaining", percentage, formattedSpeed, eta));
            } else if (percentage >= 0) {
                infoMessage(i18n("Uploading… %1% · %2/s", percentage, formattedSpeed));
            }
            return;
        }

        const QString message = object.value(QStringLiteral("msg")).toString().trimmed();
        if (!message.isEmpty()) {
            RcloneProcess::appendLimited(result.standardError, message.toUtf8() + '\n');
        }
    };
    const auto collectOutput = [&process, &result, &pendingLog, &handleLogLine](bool flush) {
        RcloneProcess::appendLimited(result.standardOutput, process.readAllStandardOutput());
        pendingLog.append(process.readAllStandardError());
        for (;;) {
            const qsizetype newline = pendingLog.indexOf('\n');
            if (newline < 0) {
                break;
            }
            handleLogLine(pendingLog.left(newline));
            pendingLog.remove(0, newline + 1);
        }
        if (flush && !pendingLog.isEmpty()) {
            handleLogLine(pendingLog);
            pendingLog.clear();
        }
    };

    infoMessage(i18n("Uploading…"));
    while (process.state() != QProcess::NotRunning) {
        if (wasKilled()) {
            result.cancelled = true;
            RcloneProcess::stopProcess(process);
            break;
        }
        process.waitForFinished(100);
        collectOutput(false);
    }
    collectOutput(true);
    result.exitCode = process.exitCode();
    return result;
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

std::optional<RcloneItem> RcloneWorker::sourceItem(const RcloneUrl &url, QString *error) const
{
    const QString fileName = url.remotePath().section(QLatin1Char('/'), -1);
    const QString parentPath = url.remotePath().section(QLatin1Char('/'), 0, -2);
    const QString parentSpec = url.remote() + QLatin1Char(':') + parentPath;

    QString listError;
    const QList<RcloneItem> items = m_backend.list(parentSpec, &listError, [this]() {
        return wasKilled();
    });
    if (!listError.isEmpty()) {
        if (error) {
            *error = listError;
        }
        return std::nullopt;
    }

    std::optional<RcloneItem> selected;
    int matches = 0;
    for (const RcloneItem &item : items) {
        if (item.name != fileName) {
            continue;
        }
        ++matches;
        if (!selected || RcloneEntryFormat::preferItem(item, *selected)) {
            selected = item;
        }
    }

    if (!selected) {
        if (error) {
            *error = QStringLiteral("object not found");
        }
        return std::nullopt;
    }
    selected->ambiguous = matches > 1;
    selected->readOnly = selected->readOnly || selected->ambiguous;
    return selected;
}

KIO::WorkerResult RcloneWorker::cacheRemoteFile(const RcloneUrl &url, RcloneItem &item)
{
    if (cachedDownloadMatches(url, item)) {
        item.size = m_cachedDownload->size();
        return KIO::WorkerResult::pass();
    }

    const QString remoteVersion = RcloneEntryFormat::itemVersion(item);
    clearCachedDownload();
    if (item.ambiguous && item.id.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ,
                                       i18n("Multiple remote objects have this name, and this backend cannot identify which one to open safely."));
    }

    const QString suffix = QFileInfo(url.remotePath()).completeSuffix();
    const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    const QString fileTemplate = QDir::tempPath() + QStringLiteral("/kio-rclone-XXXXXX") + extension;
    auto temporaryFile = std::make_unique<QTemporaryFile>(fileTemplate);
    if (!temporaryFile->open()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_OPEN_FOR_WRITING, temporaryFile->errorString());
    }
    const QString temporaryPath = temporaryFile->fileName();
    temporaryFile->close();

    RcloneResult result;
    if (!item.id.isEmpty()) {
        result = m_backend.run({QStringLiteral("backend"),
                                QStringLiteral("copyid"),
                                url.remote() + QLatin1Char(':'),
                                item.id,
                                temporaryPath,
                                QStringLiteral("--ignore-times"),
                                QStringLiteral("--log-level"),
                                QStringLiteral("ERROR")},
                               -1,
                               [this]() {
                                   return wasKilled();
                               });
    }

    if (item.id.isEmpty() || !result.success()) {
        if (result.cancelled || wasKilled()) {
            return KIO::WorkerResult::pass();
        }
        if (item.ambiguous && !item.id.isEmpty()) {
            return errorResult(result.errorMessage(), KIO::ERR_CANNOT_READ, url.url());
        }

        QFile::remove(temporaryPath);
        result = m_backend.run({QStringLiteral("copyto"),
                                url.remoteSpec(),
                                temporaryPath,
                                QStringLiteral("--ignore-times"),
                                QStringLiteral("--log-level"),
                                QStringLiteral("ERROR")},
                               -1,
                               [this]() {
                                   return wasKilled();
                               });
    }

    if (result.cancelled || wasKilled()) {
        return KIO::WorkerResult::pass();
    }
    if (!result.success()) {
        return errorResult(result.errorMessage(), KIO::ERR_CANNOT_READ, url.url());
    }
    if (!temporaryFile->open() || !temporaryFile->seek(0)) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, temporaryFile->errorString());
    }

    item.size = temporaryFile->size();
    m_cachedDownloadSpec = url.remoteSpec();
    m_cachedDownloadVersion = remoteVersion;
    m_cachedDownload = std::move(temporaryFile);
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::resolveUnknownSize(const RcloneUrl &url, RcloneItem &item)
{
    return cacheRemoteFile(url, item);
}

bool RcloneWorker::cachedDownloadMatches(const RcloneUrl &url, const RcloneItem &item) const
{
    return m_cachedDownload && m_cachedDownloadSpec == url.remoteSpec() && m_cachedDownloadVersion == RcloneEntryFormat::itemVersion(item);
}

KIO::WorkerResult RcloneWorker::sendCachedDownload(const RcloneUrl &url)
{
    if (!m_cachedDownload || !m_cachedDownload->seek(0)) {
        clearCachedDownload();
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.url().toDisplayString());
    }

    totalSize(m_cachedDownload->size());
    qint64 processed = 0;
    for (;;) {
        if (wasKilled()) {
            clearCachedDownload();
            return KIO::WorkerResult::pass();
        }

        const QByteArray chunk = m_cachedDownload->read(DownloadChunkSize);
        if (chunk.isEmpty()) {
            if (m_cachedDownload->error() != QFileDevice::NoError) {
                const QString error = m_cachedDownload->errorString();
                clearCachedDownload();
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, error);
            }
            break;
        }

        data(chunk);
        processed += chunk.size();
        processedSize(processed);
    }

    data(QByteArray());
    clearCachedDownload();
    return KIO::WorkerResult::pass();
}

void RcloneWorker::clearCachedDownload()
{
    m_cachedDownload.reset();
    m_cachedDownloadSpec.clear();
    m_cachedDownloadVersion.clear();
}

#include "rcloneworker.moc"
