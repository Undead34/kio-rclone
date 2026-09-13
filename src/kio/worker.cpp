/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "worker.h"
#include "rclone/url.h"

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
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrlQuery>

#include <limits>
#include <optional>

Q_LOGGING_CATEGORY(KIO_RCLONE, "kf.kio.workers.rclone")

namespace {

class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.rclone" FILE "rclone.json")
};

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

class WorkerRcloneContext final : public RcloneContext
{
public:
    explicit WorkerRcloneContext(const KIO::WorkerBase &worker)
        : m_worker(worker)
    {
    }

    [[nodiscard]] bool isCancelled() const override
    {
        return m_worker.wasKilled();
    }

private:
    const KIO::WorkerBase &m_worker;
};

KIO::WorkerResult malformedUrlResult(const QUrl &url)
{
    return KIO::WorkerResult::fail(
        KIO::ERR_MALFORMED_URL,
        url.toDisplayString());
}

KIO::WorkerResult rcloneFailure(const RcloneError &error,
                                const QUrl &url,
                                int fallbackError)
{
    const QString display = url.toDisplayString();

    switch (error.code) {
    case RcloneErrorCode::Cancelled:
    case RcloneErrorCode::Aborted:
        return KIO::WorkerResult::fail(
            KIO::ERR_USER_CANCELED,
            display);

    case RcloneErrorCode::NotFound:
        return KIO::WorkerResult::fail(
            KIO::ERR_DOES_NOT_EXIST,
            display);

    case RcloneErrorCode::PermissionDenied:
        return KIO::WorkerResult::fail(
            KIO::ERR_ACCESS_DENIED,
            display);

    case RcloneErrorCode::Unsupported:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            error.message);

    case RcloneErrorCode::TimedOut:
    case RcloneErrorCode::InvalidResponse:
    case RcloneErrorCode::ProcessFailure:
    case RcloneErrorCode::Unknown:
    case RcloneErrorCode::AlreadyExists:
    case RcloneErrorCode::None:
        break;
    }

    if (!error.message.isEmpty()) {
        return KIO::WorkerResult::fail(
            fallbackError,
            error.message);
    }

    return KIO::WorkerResult::fail(
        fallbackError,
        display);
}

KIO::WorkerResult listFailure(const RcloneError &error,
                              const QUrl &url)
{
    return rcloneFailure(
        error,
        url,
        KIO::ERR_CANNOT_ENTER_DIRECTORY);
}

KIO::WorkerResult statFailure(const RcloneError &error,
                              const QUrl &url)
{
    return rcloneFailure(
        error,
        url,
        KIO::ERR_CANNOT_STAT);
}

QString iconForRemoteType(const QString &type)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("drive"), QStringLiteral("folder-gdrive")},
        {QStringLiteral("dropbox"), QStringLiteral("folder-dropbox")},
        {QStringLiteral("onedrive"), QStringLiteral("folder-onedrive")},
    };
    return icons.value(type, QStringLiteral("folder-cloud"));
}

std::optional<QString> localPathFor(const RcloneLocation &location)
{
    switch (location.kind()) {
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveSharedDrives:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
    case RcloneLocation::Kind::DriveSharedDrive:
        return std::nullopt;
    }

    const QString configuredRoot =
        qEnvironmentVariable("KIO_RCLONE_VFS_ROOT");

    if (configuredRoot.isEmpty()
        || !QDir::isAbsolutePath(configuredRoot)) {
        return std::nullopt;
    }

    const QString rootPath =
        QDir::cleanPath(QDir(configuredRoot).absolutePath());

    // The bridge is opt-in and must only be advertised while its mount is
    // available.  Otherwise KIO would hand an editor a path that cannot be
    // opened and silently fall back to the remote URL.
    if (!QDir(rootPath).exists()) {
        return std::nullopt;
    }

    const QString relativePath = location.remotePath();
    const QString candidate = relativePath.isEmpty()
        ? rootPath
        : QDir(rootPath).filePath(relativePath);
    const QString cleanCandidate = QDir::cleanPath(candidate);
    const QString rootPrefix = rootPath.endsWith(QLatin1Char('/'))
        ? rootPath
        : rootPath + QLatin1Char('/');

    // RcloneLocation already rejects dot segments, but keep this boundary
    // check here as a second line of defence before exposing a local path.
    if (cleanCandidate != rootPath
        && !cleanCandidate.startsWith(rootPrefix)) {
        return std::nullopt;
    }

    return cleanCandidate;
}

KIO::UDSEntry makeEntry(const RcloneNavigationEntry &entry)
{
    KIO::UDSEntry uds;

    uds.fastInsert(KIO::UDSEntry::UDS_NAME, entry.name);

    if (!entry.displayName.isEmpty()) {
        uds.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, entry.displayName);
    }

    uds.fastInsert(
        KIO::UDSEntry::UDS_FILE_TYPE,
        entry.isDirectory ? S_IFDIR : S_IFREG);

    uds.fastInsert(
        KIO::UDSEntry::UDS_ACCESS,
        entry.readOnly
            ? (entry.isDirectory ? 0500 : 0400)
            : (entry.isDirectory ? 0700 : 0600));

    if (!entry.mimeType.isEmpty()) {
        uds.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, entry.mimeType);
    }

    if (!entry.iconName.isEmpty()) {
        uds.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, entry.iconName);
    }

    if (entry.size >= 0) {
        uds.fastInsert(KIO::UDSEntry::UDS_SIZE, entry.size);
    }

    if (entry.modificationTime.isValid()) {
        uds.fastInsert(
            KIO::UDSEntry::UDS_MODIFICATION_TIME,
            entry.modificationTime.toSecsSinceEpoch());
    }

    if (entry.hidden.has_value()) {
        uds.fastInsert(KIO::UDSEntry::UDS_HIDDEN, *entry.hidden);
    }

    if (entry.url.isValid()) {
        // Keep read-only files on the KIO path.  In particular, an exported
        // native Google document is deliberately marked read-only by the
        // worker; resolving it to a writable rclone mount would bypass that
        // safety boundary.  Read-only directories still need their local
        // root so Dolphin can enter the manually managed mount.
        if (entry.isDirectory || !entry.readOnly) {
            if (const auto location = RcloneLocation::parse(entry.url)) {
                if (const auto localPath = localPathFor(*location)) {
                    uds.fastInsert(
                        KIO::UDSEntry::UDS_LOCAL_PATH,
                        *localPath);
                }
            }
        }

        uds.fastInsert(
            KIO::UDSEntry::UDS_URL,
            entry.url.toString());
    }

    if (entry.targetUrl.isValid()) {
        uds.fastInsert(
            KIO::UDSEntry::UDS_TARGET_URL,
            entry.targetUrl.toString());
    }

    return uds;
}

QUrl withItemIdentity(QUrl url,
                      const RcloneItem &item)
{
    QUrlQuery query(url);

    if (!item.id.isEmpty()) {
        query.removeAllQueryItems(QStringLiteral("rclone-id"));
        query.addQueryItem(QStringLiteral("rclone-id"), item.id);
    }

    if (!item.originalId.isEmpty()) {
        query.removeAllQueryItems(QStringLiteral("rclone-orig-id"));
        query.addQueryItem(QStringLiteral("rclone-orig-id"), item.originalId);
    }

    url.setQuery(query);
    return url;
}

QUrl withRemoteType(QUrl url,
                    const QString &remoteType)
{
    QUrlQuery query(url);
    query.removeAllQueryItems(QStringLiteral("rclone-remote-type"));
    query.addQueryItem(QStringLiteral("rclone-remote-type"), remoteType);
    url.setQuery(query);
    return url;
}

RcloneNavigationEntry virtualDirectoryForStat(
    const RcloneLocation &location,
    const QUrl &url)
{
    QString name;
    QString iconName = QStringLiteral("folder");
    bool readOnly = true;

    switch (location.kind()) {
    case RcloneLocation::Kind::Root:
        name = QStringLiteral(".");
        break;

    case RcloneLocation::Kind::Standard:
        name = location.remoteName();
        iconName = iconForRemoteType(
            QUrlQuery(url).queryItemValue(
                QStringLiteral("rclone-remote-type")));
        readOnly = false;
        break;

    case RcloneLocation::Kind::DriveHub:
        name = location.remoteName();
        iconName = QStringLiteral("folder-gdrive");
        break;

    case RcloneLocation::Kind::DriveMyDrive:
        name = i18n("My Drive");
        iconName = QStringLiteral("user-home");
        break;

    case RcloneLocation::Kind::DriveSharedWithMe:
        name = i18n("Shared With Me");
        iconName = QStringLiteral("folder-publicshare");
        break;

    case RcloneLocation::Kind::DriveSharedDrives:
        name = i18n("Shared Drives");
        iconName = QStringLiteral("folder-cloud");
        break;

    case RcloneLocation::Kind::DriveTrash:
        name = i18n("Trash");
        iconName = QStringLiteral("user-trash-full");
        break;

    case RcloneLocation::Kind::DriveStarred:
        name = i18n("Starred");
        iconName = QStringLiteral("folder-favorites");
        break;

    case RcloneLocation::Kind::DriveSharedDrive:
        name = location.label().isEmpty()
            ? location.identifier()
            : location.label();
        iconName = QStringLiteral("folder-cloud");
        break;

    case RcloneLocation::Kind::ConfigureEntry:
        break;
    }

    RcloneNavigationEntry entry =
        RcloneNavigationEntry::virtualDirectory(
            name,
            url,
            iconName);
    entry.readOnly = readOnly;

    return entry;
}

bool isSyntheticDirectory(const RcloneLocation &location)
{
    if (location.isRoot() || location.remotePath().isEmpty()) {
        return true;
    }

    switch (location.kind()) {
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return true;

    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
    case RcloneLocation::Kind::DriveSharedDrive:
    case RcloneLocation::Kind::Root:
        return false;
    }

    return false;
}

RcloneListOptions statOptionsFor(const RcloneLocation &location)
{
    RcloneListOptions options;

    switch (location.kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
        options.extraArguments.append(
            QStringLiteral("--drive-shared-with-me"));
        break;

    case RcloneLocation::Kind::DriveStarred:
        options.extraArguments.append(
            QStringLiteral("--drive-starred-only"));
        break;

    case RcloneLocation::Kind::DriveSharedDrive:
        options.extraArguments.append(
            QStringLiteral("--drive-team-drive"));
        options.extraArguments.append(location.identifier());
        break;

    case RcloneLocation::Kind::DriveTrash:
        options.extraArguments.append(
            QStringLiteral("--drive-trashed-only"));
        break;

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveMyDrive:
    case RcloneLocation::Kind::DriveSharedDrives:
        break;
    }

    return options;
}

QString localSyncRoot()
{
    return QDir(
        QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("kio-rclone/files"));
}

} // namespace

RcloneWorker::RcloneWorker(const QByteArray &protocol,
                           const QByteArray &poolSocket,
                           const QByteArray &appSocket)
    : KIO::WorkerBase(protocol, poolSocket, appSocket)
    , m_client()
    , m_navigation(m_client)
    , m_syncRepository()
{
}


KIO::WorkerResult RcloneWorker::listDir(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_FILE,
            url.toDisplayString());
    }

    WorkerRcloneContext ctx(*this);

    const RcloneStatus status =
        location->isRoot()
            ? m_navigation.listRoot(
                  [this](const RcloneNavigationEntry &entry) {
                      listEntry(makeEntry(entry));
                      return !wasKilled();
                  },
                  ctx)
            : m_navigation.list(
                  *location,
                  [this](const RcloneNavigationEntry &entry) {
                      listEntry(makeEntry(entry));
                      return !wasKilled();
                  },
                  ctx);

    if (!status.success()) {
        return listFailure(status.error, url);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::stat(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        statEntry(makeEntry(RcloneNavigationEntry::virtualFile(
            RcloneUrl::ConfigureEntry,
            RcloneUrlBuilder::createConfigLauncher(),
            RcloneUrl::ConfigurationLauncherMimeType,
            QStringLiteral("configure"),
            i18n("Configure Remotes…"))));
        return KIO::WorkerResult::pass();
    }

    if (location->kind() == RcloneLocation::Kind::DriveSharedDrive
        && location->identifier().isEmpty()) {
        return malformedUrlResult(url);
    }

    if (isSyntheticDirectory(*location)) {
        statEntry(makeEntry(virtualDirectoryForStat(*location, url)));
        return KIO::WorkerResult::pass();
    }

    WorkerRcloneContext ctx(*this);
    const auto item = m_client.stat(
        location->toCliSpec(),
        statOptionsFor(*location),
        ctx);

    if (!item.success()) {
        return statFailure(item.error, url);
    }

    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }

    RcloneItem resolvedItem = *item.data;

    QUrl itemUrl = withItemIdentity(url, resolvedItem);

    if (location->isDriveVirtual()) {
        itemUrl = withRemoteType(itemUrl, QStringLiteral("drive"));
    }

    statEntry(makeEntry(RcloneNavigationEntry::fromItem(
        resolvedItem,
        itemUrl)));

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mimetype(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        mimeType(RcloneUrl::ConfigurationLauncherMimeType);
        return KIO::WorkerResult::pass();
    }

    if (location->kind() == RcloneLocation::Kind::DriveSharedDrive
        && location->identifier().isEmpty()) {
        return malformedUrlResult(url);
    }

    if (isSyntheticDirectory(*location)) {
        mimeType(QStringLiteral("inode/directory"));
        return KIO::WorkerResult::pass();
    }

    WorkerRcloneContext ctx(*this);
    const auto item = m_client.stat(
        location->toCliSpec(),
        statOptionsFor(*location),
        ctx);

    if (!item.success()) {
        return rcloneFailure(item.error, url, KIO::ERR_DOES_NOT_EXIST);
    }

    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }

    mimeType(item.data->isDirectory
                 ? QStringLiteral("inode/directory")
                 : item.data->mimeType.isEmpty()
                 ? QStringLiteral("application/octet-stream")
                 : item.data->mimeType);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::get(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        redirection(RcloneUrlBuilder::createConfigLauncher());
        return KIO::WorkerResult::pass();
    }

    if (isSyntheticDirectory(*location)) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_DIRECTORY,
            url.toDisplayString());
    }

    /*
     * download() todavía no recibe flags por vista. No debemos omitirlos:
     * hacerlo podría leer un homónimo de My Drive en vez del elemento que
     * Dolphin muestra en la vista filtrada.
     */
    switch (location->kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Reading files from this Google Drive view is not available yet."));

    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_DIRECTORY,
            url.toDisplayString());

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;
    }

    WorkerRcloneContext ctx(*this);
    const auto item = m_client.stat(
        location->toCliSpec(),
        statOptionsFor(*location),
        ctx);

    if (!item.success()) {
        return rcloneFailure(item.error, url, KIO::ERR_CANNOT_READ);
    }

    if (item.data->isDirectory) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_DIRECTORY,
            url.toDisplayString());
    }

    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }

    RcloneItem resolvedItem = *item.data;

    mimeType(resolvedItem.mimeType.isEmpty()
                 ? QStringLiteral("application/octet-stream")
                 : resolvedItem.mimeType);

    if (resolvedItem.size >= 0) {
        totalSize(resolvedItem.size);
    }

    qint64 processed = 0;
    const RcloneStatus status = m_client.download(
        location->toCliSpec(),
        [this, &processed](const QByteArray &chunk) {
            if (wasKilled()) {
                return false;
            }

            data(chunk);
            processed += chunk.size();
            processedSize(processed);
            return true;
        },
        ctx);

    if (!status.success()) {
        return rcloneFailure(status.error, url, KIO::ERR_CANNOT_READ);
    }

    if (resolvedItem.size >= 0 && processed != resolvedItem.size) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_READ,
            i18n("The remote file changed while it was being read."));
    }

    data(QByteArray());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::put(const QUrl &url,
                                    int permissions,
                                    KIO::JobFlags flags)
{
    Q_UNUSED(permissions)

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isRoot()
        || location->isConfigureEntry()
        || location->remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            url.toDisplayString());
    }

    switch (location->kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
        return KIO::WorkerResult::fail(
            KIO::ERR_WRITE_ACCESS_DENIED,
            i18n("This Google Drive view is read-only."));

    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            url.toDisplayString());

    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Uploading files to Shared Drives is not available yet."));

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;
    }

    WorkerRcloneContext ctx(*this);
    const auto destination = m_client.stat(
        location->toCliSpec(),
        statOptionsFor(*location),
        ctx);
    RcloneTargetSnapshot targetSnapshot;

    if (destination.success()) {
        targetSnapshot =
            RcloneTargetSnapshot::fromItem(*destination.data);

        if (destination.data->isDirectory) {
            return KIO::WorkerResult::fail(
                KIO::ERR_DIR_ALREADY_EXIST,
                url.toDisplayString());
        }

        if (destination.data->readOnly) {
            return KIO::WorkerResult::fail(
                KIO::ERR_WRITE_ACCESS_DENIED,
                url.toDisplayString());
        }

        if (!(flags & KIO::Overwrite)) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                url.toDisplayString());
        }
    } else if (destination.error.code != RcloneErrorCode::NotFound) {
        return rcloneFailure(destination.error, url, KIO::ERR_CANNOT_STAT);
    }

    bool sizeOk = false;
    const qint64 sourceSize = metaData(
        QStringLiteral("sourceSize")).toLongLong(&sizeOk);

    if (sizeOk && sourceSize >= 0) {
        totalSize(sourceSize);
    }

    const QString suffix = QFileInfo(
        location->remotePath()).completeSuffix();
    const QString extension = suffix.isEmpty()
        ? QString()
        : QLatin1Char('.') + suffix;
    QTemporaryFile localUpload(
        QDir::tempPath()
        + QStringLiteral("/kio-rclone-upload-XXXXXX")
        + extension);

    if (!localUpload.open()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_OPEN_FOR_WRITING,
            localUpload.errorString());
    }

    qint64 stagedSize = 0;
    infoMessage(i18n("Preparing upload…"));

    while (true) {
        if (wasKilled()) {
            return KIO::WorkerResult::pass();
        }

        dataReq();
        QByteArray sourceData;
        const int readResult = readData(sourceData);

        if (readResult < 0) {
            return KIO::WorkerResult::fail(
                KIO::ERR_CANNOT_READ,
                url.toDisplayString());
        }

        if (readResult == 0) {
            break;
        }

        if (sizeOk && sourceSize >= 0
            && stagedSize > sourceSize - sourceData.size()) {
            return KIO::WorkerResult::fail(
                KIO::ERR_CANNOT_WRITE,
                i18n("The upload source changed size while it was being read."));
        }

        if (localUpload.write(sourceData) != sourceData.size()) {
            return KIO::WorkerResult::fail(
                KIO::ERR_CANNOT_WRITE,
                localUpload.errorString());
        }

        stagedSize += sourceData.size();
    }

    if (sizeOk && sourceSize >= 0 && stagedSize != sourceSize) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            i18n("The upload source changed size while it was being read."));
    }

    if (!localUpload.flush()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            localUpload.errorString());
    }

    const QString localUploadPath = localUpload.fileName();
    localUpload.close();

    if (!sizeOk || sourceSize < 0) {
        totalSize(stagedSize);
    }

    RcloneWriteOptions options;
    options.replaceExisting = flags & KIO::Overwrite;
    options.expectedTarget = targetSnapshot;

    infoMessage(i18n("Uploading…"));
    const RcloneStatus status = m_client.upload(
        localUploadPath,
        location->toCliSpec(),
        [this](const RcloneClient::TransferStats &progress) {
            if (wasKilled()) {
                return;
            }

            processedSize(progress.bytes);

            if (progress.speed >= 0) {
                const quint64 maximumSpeed =
                    static_cast<quint64>(
                        std::numeric_limits<unsigned long>::max());
                const quint64 boundedSpeed = qMin(
                    static_cast<quint64>(progress.speed),
                    maximumSpeed);
                speed(static_cast<unsigned long>(boundedSpeed));
            }
        },
        options,
        ctx);

    if (!status.success()) {
        if (status.error.code == RcloneErrorCode::AlreadyExists) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                url.toDisplayString());
        }

        return rcloneFailure(status.error, url, KIO::ERR_CANNOT_WRITE);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url, int permissions)
{
    Q_UNUSED(permissions)

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    /*
     * La raíz del worker y los índices de Drive no representan directorios
     * remotos sobre los que rclone pueda crear algo. El nombre nuevo que KIO
     * entrega sí tendrá una ruta remota no vacía.
     */
    if (location->isRoot()
        || location->isConfigureEntry()
        || location->remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_MKDIR,
            url.toDisplayString());
    }

    /*
     * Estas vistas de Drive son filtros de navegación. No podemos enviarlas
     * como una creación normal: sin sus flags, rclone podría crear la carpeta
     * en My Drive en vez de en la vista que Dolphin está mostrando.
     */
    switch (location->kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
        return KIO::WorkerResult::fail(
            KIO::ERR_WRITE_ACCESS_DENIED,
            i18n("This Google Drive view is read-only."));

    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_MKDIR,
            url.toDisplayString());

    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Creating folders in Shared Drives is not available yet."));

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;
    }

    WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_client.mkdir(
        location->toCliSpec(),
        ctx);

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            url,
            KIO::ERR_CANNOT_MKDIR);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::rename(const QUrl &src,
                                       const QUrl &dest,
                                       KIO::JobFlags flags)
{
    const auto source = RcloneLocation::parse(src);
    const auto destination = RcloneLocation::parse(dest);

    if (!source || !destination) {
        return malformedUrlResult(source ? dest : src);
    }

    if (source->isRoot()
        || source->isConfigureEntry()
        || source->remotePath().isEmpty()
        || destination->isRoot()
        || destination->isConfigureEntry()
        || destination->remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_RENAME,
            src.toDisplayString());
    }

    const auto isDirectMoveLocation = [](const RcloneLocation &location) {
        return location.kind() == RcloneLocation::Kind::Standard
            || location.kind() == RcloneLocation::Kind::DriveMyDrive;
    };

    if (!isDirectMoveLocation(*source)
        || !isDirectMoveLocation(*destination)) {
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Moving items in this Google Drive view is not available yet."));
    }

    if (source->remoteName() != destination->remoteName()
        || source->kind() != destination->kind()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Moving between different rclone remotes or Google Drive views is not available yet."));
    }

    if (source->toCliSpec() == destination->toCliSpec()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IDENTICAL_FILES,
            src.toDisplayString());
    }

    WorkerRcloneContext ctx(*this);
    const auto existingDestination = m_client.stat(
        destination->toCliSpec(),
        statOptionsFor(*destination),
        ctx);

    if (existingDestination.success()) {
        if (existingDestination.data->isDirectory) {
            return KIO::WorkerResult::fail(
                KIO::ERR_DIR_ALREADY_EXIST,
                dest.toDisplayString());
        }

        if (!(flags & KIO::Overwrite)) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                dest.toDisplayString());
        }
    } else if (existingDestination.error.code != RcloneErrorCode::NotFound) {
        return rcloneFailure(
            existingDestination.error,
            dest,
            KIO::ERR_CANNOT_STAT);
    }

    RcloneWriteOptions options;
    options.replaceExisting = flags & KIO::Overwrite;

    const RcloneStatus status = m_client.move(
        source->toCliSpec(),
        destination->toCliSpec(),
        options,
        ctx);

    if (!status.success()) {
        if (status.error.code == RcloneErrorCode::AlreadyExists) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                dest.toDisplayString());
        }

        return rcloneFailure(status.error, src, KIO::ERR_CANNOT_RENAME);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::copy(const QUrl &src,
                                     const QUrl &dest,
                                     int permissions,
                                     KIO::JobFlags flags)
{
    Q_UNUSED(permissions)

    const auto source = RcloneLocation::parse(src);
    const auto destination = RcloneLocation::parse(dest);

    if (!source || !destination) {
        return malformedUrlResult(source ? dest : src);
    }

    if (source->isRoot()
        || source->isConfigureEntry()
        || source->remotePath().isEmpty()
        || destination->isRoot()
        || destination->isConfigureEntry()
        || destination->remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            dest.toDisplayString());
    }

    const auto isDirectCopyLocation = [](const RcloneLocation &location) {
        return location.kind() == RcloneLocation::Kind::Standard
            || location.kind() == RcloneLocation::Kind::DriveMyDrive;
    };

    if (!isDirectCopyLocation(*source)
        || !isDirectCopyLocation(*destination)) {
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Copying items in this Google Drive view is not available yet."));
    }

    if (source->toCliSpec() == destination->toCliSpec()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IDENTICAL_FILES,
            src.toDisplayString());
    }

    WorkerRcloneContext ctx(*this);
    const auto existingDestination = m_client.stat(
        destination->toCliSpec(),
        statOptionsFor(*destination),
        ctx);

    if (existingDestination.success()) {
        if (existingDestination.data->isDirectory) {
            return KIO::WorkerResult::fail(
                KIO::ERR_DIR_ALREADY_EXIST,
                dest.toDisplayString());
        }

        if (!(flags & KIO::Overwrite)) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                dest.toDisplayString());
        }
    } else if (existingDestination.error.code != RcloneErrorCode::NotFound) {
        return rcloneFailure(
            existingDestination.error,
            dest,
            KIO::ERR_CANNOT_STAT);
    }

    RcloneWriteOptions options;
    options.replaceExisting = flags & KIO::Overwrite;

    const RcloneStatus status = m_client.copy(
        source->toCliSpec(),
        destination->toCliSpec(),
        options,
        ctx);

    if (!status.success()) {
        if (status.error.code == RcloneErrorCode::AlreadyExists) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                dest.toDisplayString());
        }

        return rcloneFailure(status.error, dest, KIO::ERR_CANNOT_WRITE);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::del(const QUrl &url,
                                    bool isFile)
{
    const int fallbackError = isFile
        ? KIO::ERR_CANNOT_DELETE
        : KIO::ERR_CANNOT_RMDIR;

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isRoot()
        || location->isConfigureEntry()
        || location->remotePath().isEmpty()) {
        return KIO::WorkerResult::fail(
            fallbackError,
            url.toDisplayString());
    }

    /*
     * Estas vistas de Drive son filtros de navegación. Borrar con su ruta
     * base, sin los flags de la vista, podría afectar un elemento distinto
     * de My Drive.
     */
    switch (location->kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
        return KIO::WorkerResult::fail(
            KIO::ERR_WRITE_ACCESS_DENIED,
            i18n("This Google Drive view is read-only."));

    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return KIO::WorkerResult::fail(
            fallbackError,
            url.toDisplayString());

    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Deleting items in Shared Drives is not available yet."));

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;
    }

    RcloneRemovalMode mode = RcloneRemovalMode::File;

    if (!isFile) {
        mode = metaData(QStringLiteral("recurse"))
            == QLatin1String("true")
            ? RcloneRemovalMode::Recursive
            : RcloneRemovalMode::EmptyDirectory;
    }

    WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_client.remove(
        location->toCliSpec(),
        mode,
        ctx);

    if (!status.success()) {
        return rcloneFailure(status.error, url, fallbackError);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::fileSystemFreeSpace(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location || location->isRoot() || location->isConfigureEntry()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_STAT,
            url.toDisplayString());
    }

    /*
     * spaceInfo() todavía no acepta --drive-team-drive. No devolvemos la
     * cuota de My Drive como si fuera la de una unidad compartida distinta.
     */
    if (location->kind() == RcloneLocation::Kind::DriveSharedDrive) {
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Storage information for Shared Drives is not available yet."));
    }

    WorkerRcloneContext ctx(*this);
    const auto space = m_client.spaceInfo(
        location->remoteName() + QLatin1Char(':'),
        ctx);

    if (!space.success()) {
        return rcloneFailure(
            space.error,
            url,
            KIO::ERR_UNSUPPORTED_ACTION);
    }

    if (space.data->total >= 0) {
        setMetaData(
            QStringLiteral("total"),
            QString::number(space.data->total));
    }

    if (space.data->free >= 0) {
        setMetaData(
            QStringLiteral("available"),
            QString::number(space.data->free));
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::open(const QUrl &url,
                   QIODevice::OpenMode mode)
{
    if (m_openFile) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_OPEN_FOR_READING,
            url.toDisplayString());
    }

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (location->isRoot()
        || location->isConfigureEntry()
        || location->remotePath().isEmpty()
        || isSyntheticDirectory(*location)) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_DIRECTORY,
            url.toDisplayString());
    }

    /*
     * Por ahora RemoteFile opera sobre una especificación directa de rclone.
     * Las vistas virtuales que requieren flags adicionales se pueden integrar
     * después pasando RcloneListOptions/RcloneLocation al modelo de archivo.
     */
    switch (location->kind()) {
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;

    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Opening files from this Google Drive view is not available yet."));

    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
    case RcloneLocation::Kind::Root:
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_DIRECTORY,
            url.toDisplayString());

    case RcloneLocation::Kind::ConfigureEntry:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            url.toDisplayString());
    }

    WorkerRcloneContext ctx(*this);

    RcloneCacheConfig cacheConfig;
    cacheConfig.baseCacheDirectory = localSyncRoot();

    auto file = std::make_unique<RcloneRemoteFile>(
        m_client,
        m_syncRepository,
        cacheConfig);

    const RcloneStatus status =
        file->open(
            location->toCliSpec(),
            mode,
            ctx);

    if (!status.success()) {
        const int fallback =
            mode.testFlag(QIODevice::WriteOnly)
            ? KIO::ERR_CANNOT_OPEN_FOR_WRITING
            : KIO::ERR_CANNOT_OPEN_FOR_READING;

        return rcloneFailure(
            status.error,
            url,
            fallback);
    }

    m_openUrl = url;
    m_openFile = std::move(file);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::read(KIO::filesize_t size)
{
    if (!m_openFile || !m_openFile->isOpen()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_READ,
            m_openUrl.toDisplayString());
    }

    const auto result =
        m_openFile->read(
            static_cast<qint64>(size));

    if (!result.success()) {
        return rcloneFailure(
            result.error,
            m_openUrl,
            KIO::ERR_CANNOT_READ);
    }

    data(*result.data);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::write(const QByteArray &buffer)
{
    if (!m_openFile || !m_openFile->isOpen()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            m_openUrl.toDisplayString());
    }

    const RcloneStatus status =
        m_openFile->write(buffer);

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            m_openUrl,
            KIO::ERR_CANNOT_WRITE);
    }

    written(buffer.size());

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::seek(KIO::filesize_t offset)
{
    if (!m_openFile || !m_openFile->isOpen()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_SEEK,
            m_openUrl.toDisplayString());
    }

    const RcloneStatus status =
        m_openFile->seek(
            static_cast<qint64>(offset));

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            m_openUrl,
            KIO::ERR_CANNOT_SEEK);
    }

    position(
        static_cast<KIO::filesize_t>(
            m_openFile->position()));

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::truncate(KIO::filesize_t length)
{
    if (!m_openFile || !m_openFile->isOpen()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_TRUNCATE,
            m_openUrl.toDisplayString());
    }

    const RcloneStatus status =
        m_openFile->truncate(
            static_cast<qint64>(length));

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            m_openUrl,
            KIO::ERR_CANNOT_TRUNCATE);
    }

    truncated(length);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::close()
{
    if (!m_openFile) {
        return KIO::WorkerResult::pass();
    }

    WorkerRcloneContext ctx(*this);

    const QUrl url = m_openUrl;

    const RcloneStatus status =
        m_openFile->close(ctx);

    /*
     * RemoteFile ya persistió su estado en SyncRepository.
     *
     * Si el upload falló:
     *
     *     local = Modified
     *
     * y NO perdemos los datos aunque destruyamos el objeto temporal.
     */
    m_openFile.reset();
    m_openUrl = {};

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            url,
            KIO::ERR_CANNOT_WRITE);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::setModificationTime(const QUrl &url,
                                  const QDateTime &mtime)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return malformedUrlResult(url);
    }

    if (!mtime.isValid()
        || location->isRoot()
        || location->isConfigureEntry()
        || location->remotePath().isEmpty()
        || isSyntheticDirectory(*location)) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_SETTIME,
            url.toDisplayString());
    }

    switch (location->kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
        return KIO::WorkerResult::fail(
            KIO::ERR_WRITE_ACCESS_DENIED,
            i18n("This Google Drive view is read-only."));

    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("Changing modification times in Shared Drives is not available yet."));

    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_SETTIME,
            url.toDisplayString());

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
        break;
    }

    WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_client.setModificationTime(
        location->toCliSpec(),
        mtime,
        ctx);

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            url,
            KIO::ERR_CANNOT_SETTIME);
    }

    return KIO::WorkerResult::pass();
}

/// Privates


#include "worker.moc"
