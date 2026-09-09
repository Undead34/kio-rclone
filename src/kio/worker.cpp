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

} // namespace

RcloneWorker::RcloneWorker(const QByteArray &protocol,
                           const QByteArray &poolSocket,
                           const QByteArray &appSocket)
    : KIO::WorkerBase(protocol, poolSocket, appSocket)
    , m_client()
    , m_navigation(m_client)
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

    QUrl itemUrl = withItemIdentity(url, *item.data);

    if (location->isDriveVirtual()) {
        itemUrl = withRemoteType(itemUrl, QStringLiteral("drive"));
    }

    statEntry(makeEntry(RcloneNavigationEntry::fromItem(
        *item.data,
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

    mimeType(item.data->mimeType.isEmpty()
                 ? QStringLiteral("application/octet-stream")
                 : item.data->mimeType);

    if (item.data->size >= 0) {
        totalSize(item.data->size);
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

    if (item.data->size >= 0 && processed != item.data->size) {
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

    if (destination.success()) {
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

KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url,
                                      int permissions)
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
    const bool writing = mode.testFlag(QIODevice::WriteOnly);
    const int fallbackError = writing
        ? KIO::ERR_CANNOT_OPEN_FOR_WRITING
        : KIO::ERR_CANNOT_OPEN_FOR_READING;

    if (m_openFile) {
        return KIO::WorkerResult::fail(
            fallbackError,
            i18n("Another remote file is already open."));
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
     * FileJob reads must use exactly the same view as Dolphin. downloadRange()
     * and upload() currently accept a plain remote spec only; accepting a
     * filtered Drive view here could silently target a same-named My Drive
     * item instead.
     */
    switch (location->kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
    case RcloneLocation::Kind::DriveSharedDrive:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            i18n("File access in this Google Drive view is not available yet."));

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
    auto file = std::make_unique<RcloneRemoteFile>(m_client);
    const RcloneStatus status = file->open(
        location->toCliSpec(),
        mode,
        ctx);

    if (!status.success()) {
        if (status.error.code == RcloneErrorCode::AlreadyExists) {
            return KIO::WorkerResult::fail(
                KIO::ERR_FILE_ALREADY_EXIST,
                url.toDisplayString());
        }

        return rcloneFailure(status.error, url, fallbackError);
    }

    m_openUrl = url;
    m_openFile = std::move(file);

    // KIO::FileJob starts with a size of zero. It must receive the remote
    // size before this method returns (and therefore before its open signal)
    // so consumers such as KIO-FUSE do not issue read(0).
    if (m_openFile->size() >= 0) {
        totalSize(static_cast<KIO::filesize_t>(m_openFile->size()));
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::read(KIO::filesize_t size)
{
    if (!m_openFile) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_READ,
            i18n("No remote file is open."));
    }

    if (size > static_cast<KIO::filesize_t>(
                   std::numeric_limits<qint64>::max())) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_READ,
            i18n("The requested read size is too large."));
    }

    WorkerRcloneContext ctx(*this);
    const auto result = m_openFile->read(
        static_cast<qint64>(size),
        ctx);

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
    if (!m_openFile) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_WRITE,
            i18n("No remote file is open."));
    }

    WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_openFile->write(buffer, ctx);

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
    if (!m_openFile) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_SEEK,
            i18n("No remote file is open."));
    }

    if (offset > static_cast<KIO::filesize_t>(
                     std::numeric_limits<qint64>::max())) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_SEEK,
            i18n("The requested offset is too large."));
    }

    const RcloneStatus status = m_openFile->seek(
        static_cast<qint64>(offset));

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            m_openUrl,
            KIO::ERR_CANNOT_SEEK);
    }

    position(static_cast<KIO::filesize_t>(m_openFile->position()));
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult
RcloneWorker::truncate(KIO::filesize_t length)
{
    if (!m_openFile) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_TRUNCATE,
            i18n("No remote file is open."));
    }

    if (length > static_cast<KIO::filesize_t>(
                     std::numeric_limits<qint64>::max())) {
        return KIO::WorkerResult::fail(
            KIO::ERR_CANNOT_TRUNCATE,
            i18n("The requested length is too large."));
    }

    const RcloneStatus status = m_openFile->truncate(
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
    const QUrl openUrl = m_openUrl;
    const RcloneStatus status = m_openFile->close(ctx);

    m_openFile.reset();
    m_openUrl = QUrl();

    if (!status.success()) {
        return rcloneFailure(
            status.error,
            openUrl,
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

// /*
//  * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
//  *
//  * SPDX-License-Identifier: GPL-2.0-or-later
//  */

// #include "worker.h"
// #include "cache/directorylistingpolicy.h"
// #include "directorynotifier.h"
// #include "entrybuilder.h"
// #include "rclone/entryformat.h"
// #include "rclone/process.h"
// #include "rclone/recursivelisting.h"
// #include "rclone/url.h"

// #include <KLocalizedString>

// #include <QCoreApplication>
// #include <QDir>
// #include <QFile>
// #include <QFileInfo>
// #include <QHash>
// #include <QJsonArray>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QLocale>
// #include <QLoggingCategory>
// #include <QProcess>

// #include <algorithm>
// #include <limits>
// #include <optional>
// #include <utility>

// Q_LOGGING_CATEGORY(KIO_RCLONE, "kf.kio.workers.rclone")

// namespace
// {
// constexpr qsizetype DownloadChunkSize = 64 * 1024;

// KIO::WorkerResult configurationEntryMutationError(int error)
// {
//     return KIO::WorkerResult::fail(error, i18n("“Configure Remotes…” is a virtual launcher and cannot be modified."));
// }

// bool canRepresentInKio(const RcloneItem &item)
// {
//     // UDS_NAME becomes a URL path component. KIO reserves dot components, and
//     // neither literal slashes nor NUL are representable in one component.
//     // rclone documents that Drive can expose such names, so never let them
//     // escape into a KIO URL with changed meaning.
//     return !item.name.isEmpty() && !item.name.contains(QLatin1Char('/')) && item.name != QLatin1String(".")
//         && item.name != QLatin1String("..") && !item.name.contains(QChar::Null)
//         && (item.path.isEmpty() || item.path == item.name);
// }

// QString driveDisplayName(RcloneLocation::Kind kind, const QString &identifier = {})
// {
//     switch (kind) {
//     case RcloneLocation::Kind::DriveMyDrive:
//         return i18n("My Drive");
//     case RcloneLocation::Kind::DriveSharedWithMe:
//         return i18n("Shared With Me");
//     case RcloneLocation::Kind::DriveSharedDrives:
//         return i18n("Shared Drives");
//     case RcloneLocation::Kind::DriveTrash:
//         return i18n("Trash");
//     case RcloneLocation::Kind::DriveStarred:
//         return i18n("Starred");
//     case RcloneLocation::Kind::DriveFolders:
//         return i18n("Folders by ID");
//     case RcloneLocation::Kind::DriveFolder:
//         return i18n("Folder %1", identifier);
//     case RcloneLocation::Kind::DriveSharedDrive:
//         return identifier;
//     case RcloneLocation::Kind::DriveHub:
//     case RcloneLocation::Kind::Standard:
//         break;
//     }
//     return {};
// }

// QString driveIconName(RcloneLocation::Kind kind)
// {
//     switch (kind) {
//     case RcloneLocation::Kind::DriveHub:
//         return QStringLiteral("folder-gdrive");
//     case RcloneLocation::Kind::DriveMyDrive:
//         return QStringLiteral("user-home");
//     case RcloneLocation::Kind::DriveSharedWithMe:
//         return QStringLiteral("folder-publicshare");
//     case RcloneLocation::Kind::DriveSharedDrives:
//     case RcloneLocation::Kind::DriveSharedDrive:
//         return QStringLiteral("folder-cloud");
//     case RcloneLocation::Kind::DriveTrash:
//         return QStringLiteral("user-trash-full");
//     case RcloneLocation::Kind::DriveStarred:
//         return QStringLiteral("folder-favorites");
//     case RcloneLocation::Kind::DriveFolders:
//     case RcloneLocation::Kind::DriveFolder:
//         return QStringLiteral("folder");
//     case RcloneLocation::Kind::Standard:
//         break;
//     }
//     return QStringLiteral("folder");
// }

// QUrl parentDirectoryUrl(const QUrl &url)
// {
//     QUrl parent = url;
//     const QString path = parent.path(QUrl::FullyDecoded);
//     const qsizetype separator = path.lastIndexOf(QLatin1Char('/'));
//     parent.setPath(separator <= 0 ? QStringLiteral("/") : path.left(separator + 1));
//     return parent;
// }

// bool bypassesDirectorySnapshot(const QString &cacheControl)
// {
//     return cacheControl.compare(QLatin1String("reload"), Qt::CaseInsensitive) == 0
//         || cacheControl.compare(QLatin1String("refresh"), Qt::CaseInsensitive) == 0;
// }

// QString syntheticTrashItemKey(const QString &remote, const QString &relativePath)
// {
//     return remote + QLatin1Char('\n') + relativePath;
// }

// class KIOPluginForMetaData : public QObject
// {
//     Q_OBJECT
//     Q_PLUGIN_METADATA(IID "org.kde.kio.worker.rclone" FILE "rclone.json")
// };
// } // namespace

// extern "C" Q_DECL_EXPORT int kdemain(int argc, char **argv)
// {
//     QCoreApplication app(argc, argv);
//     app.setApplicationName(QStringLiteral("kio_rclone"));

//     if (argc != 4) {
//         qCritical("Usage: kio_rclone protocol domain-socket1 domain-socket2");
//         return 1;
//     }

//     RcloneWorker worker(argv[1], argv[2], argv[3]);
//     worker.dispatchLoop();
//     return 0;
// }

// RcloneWorker::RcloneWorker(const QByteArray &protocol, const QByteArray &poolSocket, const QByteArray &appSocket)
//     : WorkerBase(protocol, poolSocket, appSocket)
// {
// }


// std::optional<RcloneLocation> RcloneWorker::resolveLocation(const RcloneUrl &url, QString *error)
// {
//     const std::optional<bool> isDrive = isGoogleDriveRemote(url.remote(), error);
//     if (!isDrive) {
//         return std::nullopt;
//     }
//     if (!*isDrive) {
//         return RcloneLocation::standard(url.remote(), url.remotePath());
//     }

//     const std::optional<RcloneLocation> location = RcloneLocation::googleDrive(url.remote(), url.remotePath());
//     if (!location && error) {
//         *error = i18n("This Google Drive location is not valid. Open My Drive to browse the normal Drive root.");
//     }
//     return location;
// }

// std::optional<bool> RcloneWorker::isGoogleDriveRemote(const QString &remote, QString *error)
// {
//     const auto cached = m_remoteTypes.constFind(remote);
//     if (cached != m_remoteTypes.cend()) {
//         return *cached == QLatin1String("drive");
//     }

//     QString remoteError;
//     const QList<RcloneRemote> remotes = m_rclone.remoteList(&remoteError, [this]() {
//         return wasKilled();
//     });
//     if (wasKilled()) {
//         return std::nullopt;
//     }
//     if (!remoteError.isEmpty()) {
//         if (error) {
//             *error = remoteError;
//         }
//         return std::nullopt;
//     }

//     for (const RcloneRemote &candidate : remotes) {
//         m_remoteTypes.insert(candidate.name, candidate.type);
//     }
//     const auto type = m_remoteTypes.constFind(remote);
//     if (type == m_remoteTypes.cend()) {
//         if (error) {
//             *error = i18n("The rclone remote “%1” was not found.", remote);
//         }
//         return std::nullopt;
//     }
//     return *type == QLatin1String("drive");
// }

// KIO::UDSEntry RcloneWorker::currentDirectoryEntry(const RcloneLocation &location, const QString &entryName) const
// {
//     if (location.kind() == RcloneLocation::Kind::Standard) {
//         return KioEntryBuilder::remote(location.remote(), entryName == QLatin1String("."), m_remoteTypes.value(location.remote()));
//     }

//     QString displayName = driveDisplayName(location.kind(), location.identifier());
//     if (location.kind() == RcloneLocation::Kind::DriveHub) {
//         displayName = location.remote();
//     } else if (location.kind() == RcloneLocation::Kind::DriveSharedDrive) {
//         displayName = m_sharedDriveNames.value(location.remote() + QLatin1Char('\n') + location.identifier(), displayName);
//     }
//     return KioEntryBuilder::directory(entryName,
//                                       displayName,
//                                       driveIconName(location.kind()),
//                                       location.canWrite());
// }

// KIO::WorkerResult RcloneWorker::listRemoteDirectory(const QUrl &requestUrl,
//                                                      const RcloneLocation &directory,
//                                                      const DirectoryListingPolicy &policy)
// {
//     const std::optional<bool> duplicateNames = remoteMayHaveDuplicateNames(directory);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }

//     if (duplicateNames && !*duplicateNames) {
//         return listUniqueDirectory(requestUrl, directory, policy);
//     }

//     return listDuplicateSafeDirectory(requestUrl, directory, policy);
// }

// KIO::WorkerResult RcloneWorker::listUniqueDirectory(const QUrl &requestUrl,
//                                                      const RcloneLocation &directory,
//                                                      const DirectoryListingPolicy &policy)
// {
//     // rclone guarantees this remote has no duplicate names. Send each entry to
//     // KIO as it arrives; WorkerBase batches IPC internally, avoiding a JSON
//     // array and UDSEntryList peak for ordinary remotes.
//     listEntry(currentDirectoryEntry(directory));

//     QList<RcloneItem> cacheItems;
//     bool cacheableSnapshot = policy.allowsSnapshots();
//     QString error;
//     const bool listed = m_rclone.listStreaming(
//         directory.rcloneSpec(),
//         [this, &cacheItems, &cacheableSnapshot](const RcloneItem &item) {
//             if (wasKilled()) {
//                 return false;
//             }
//             if (!canRepresentInKio(item)) {
//                 return true;
//             }

//             listEntry(KioEntryBuilder::item(item));
//             if (cacheableSnapshot) {
//                 if (cacheItems.size() >= DirectorySnapshotCache::MaximumItemCount) {
//                     cacheItems.clear();
//                     cacheableSnapshot = false;
//                 } else {
//                     cacheItems.append(item);
//                 }
//             }
//             return true;
//         },
//         &error,
//         [this]() {
//             return wasKilled();
//         });
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!listed) {
//         return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl);
//     }
//     if (cacheableSnapshot) {
//         static_cast<void>(m_directorySnapshots.store(directory.remote(), directory.cachePath(), cacheItems));
//     }
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::listDuplicateSafeDirectory(const QUrl &requestUrl,
//                                                             const RcloneLocation &directory,
//                                                             const DirectoryListingPolicy &policy)
// {
//     // The capability probe is deliberately fail-safe. If an older rclone or a
//     // backend cannot describe itself, retain one read-only representative per
//     // name rather than exposing ambiguous URLs to KIO.
//     QString error;

//     QList<RcloneItem> visibleItems;
//     QHash<QString, qsizetype> itemIndexes;
//     const bool listed = m_rclone.listStreaming(
//         directory.rcloneSpec(),
//         [this, &visibleItems, &itemIndexes](const RcloneItem &item) {
//             if (wasKilled()) {
//                 return false;
//             }
//             if (!canRepresentInKio(item)) {
//                 return true;
//             }

//             const auto existing = itemIndexes.constFind(item.name);
//             if (existing == itemIndexes.cend()) {
//                 itemIndexes.insert(item.name, visibleItems.size());
//                 visibleItems.append(item);
//                 return true;
//             }

//             RcloneItem &selected = visibleItems[*existing];
//             if (RcloneEntryFormat::preferItem(item, selected)) {
//                 selected = item;
//             }
//             selected.ambiguous = true;
//             selected.readOnly = true;
//             return true;
//         },
//         &error,
//         [this]() {
//             return wasKilled();
//         });
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!listed) {
//         return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl);
//     }

//     publishDirectoryEntries(directory, visibleItems);
//     if (policy.allowsSnapshots()) {
//         static_cast<void>(m_directorySnapshots.store(directory.remote(), directory.cachePath(), visibleItems));
//     }
//     return KIO::WorkerResult::pass();
// }

// std::optional<QList<RcloneItem>> RcloneWorker::cachedDirectory(const RcloneLocation &directory,
//                                                                 const DirectoryListingPolicy &policy)
// {
//     if (!policy.allowsSnapshots()) {
//         return std::nullopt;
//     }
//     return m_directorySnapshots.load(directory.remote(), directory.cachePath(), policy.freshnessSeconds);
// }

// void RcloneWorker::publishDirectoryEntries(const RcloneLocation &directory, const QList<RcloneItem> &items)
// {
//     KIO::UDSEntryList entries;
//     entries.reserve(items.size() + 1);
//     entries.append(currentDirectoryEntry(directory));
//     for (const RcloneItem &item : items) {
//         entries.append(KioEntryBuilder::item(item));
//     }
//     listEntries(entries);
// }

// KIO::WorkerResult RcloneWorker::stat(const QUrl &url)
// {
//     const RcloneUrl rcloneUrl(url);
//     if (!rcloneUrl.isValid()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }
//     if (rcloneUrl.isRoot()) {
//         statEntry(KioEntryBuilder::root());
//         return KIO::WorkerResult::pass();
//     }
//     if (rcloneUrl.isConfigureEntry()) {
//         statEntry(KioEntryBuilder::configure());
//         return KIO::WorkerResult::pass();
//     }
//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_CANNOT_STAT, url);
//     }
//     if (location->isVirtualDirectory() || location->isPhysicalRoot()) {
//         const QString entryName = rcloneUrl.isRemoteRoot() ? rcloneUrl.remote()
//                                                             : rcloneUrl.remotePath().section(QLatin1Char('/'), -1);
//         statEntry(currentDirectoryEntry(*location, entryName));
//         return KIO::WorkerResult::pass();
//     }

//     if (const auto cachedItem = cachedItemForReadOnlyRequest(*location)) {
//         statEntry(KioEntryBuilder::item(*cachedItem));
//         return KIO::WorkerResult::pass();
//     }

//     QString error;
//     const auto item = sourceItem(*location, &error);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!item) {
//         return errorResult(error, KIO::ERR_CANNOT_STAT, url);
//     }

//     RcloneItem resolvedItem = *item;
//     if (!resolvedItem.isDirectory && resolvedItem.size < 0) {
//         if (const auto result = resolveUnknownSize(url, *location, resolvedItem); !result.success()) {
//             return result;
//         }
//         if (wasKilled()) {
//             return KIO::WorkerResult::pass();
//         }
//     }
//     statEntry(KioEntryBuilder::item(resolvedItem));
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::mimetype(const QUrl &url)
// {
//     const RcloneUrl rcloneUrl(url);

//     if (!rcloneUrl.isValid()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }

//     if (rcloneUrl.isConfigureEntry()) {
//         mimeType(RcloneUrl::ConfigurationLauncherMimeType);
//         return KIO::WorkerResult::pass();
//     }

//     if (rcloneUrl.isRoot() || rcloneUrl.isRemoteRoot()) {
//         mimeType(QStringLiteral("inode/directory"));
//         return KIO::WorkerResult::pass();
//     }

//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_DOES_NOT_EXIST, url);
//     }
//     if (location->isVirtualDirectory() || location->isPhysicalRoot()) {
//         mimeType(QStringLiteral("inode/directory"));
//         return KIO::WorkerResult::pass();
//     }

//     if (const auto cachedItem = cachedItemForReadOnlyRequest(*location)) {
//         mimeType(RcloneEntryFormat::fallbackMimeType(*cachedItem));
//         return KIO::WorkerResult::pass();
//     }

//     QString error;
//     const auto item = sourceItem(*location, &error);

//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }

//     if (!item) {
//         return errorResult(error, KIO::ERR_DOES_NOT_EXIST, url);
//     }

//     mimeType(RcloneEntryFormat::fallbackMimeType(*item));
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::get(const QUrl &url)
// {
//     const RcloneUrl rcloneUrl(url);

//     if (!rcloneUrl.isValid()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }

//     if (rcloneUrl.isConfigureEntry()) {
//         redirection(RcloneUrl::configurationLauncherUrl());
//         return KIO::WorkerResult::pass();
//     }

//     if (rcloneUrl.isRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
//     }

//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_CANNOT_READ, url);
//     }
//     if (location->isVirtualDirectory() || location->isPhysicalRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
//     }
//     if (location->isDriveTrash()) {
//         if (const auto cachedItem = cachedItemForReadOnlyRequest(*location); cachedItem && cachedItem->syntheticDirectory) {
//             return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString());
//         }
//     }

//     QString error;
//     auto item = sourceItem(*location, &error);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!item) {
//         return errorResult(error, KIO::ERR_CANNOT_READ, url);
//     }

//     mimeType(RcloneEntryFormat::fallbackMimeType(*item));
//     if (cachedDownloadMatches(*location, *item)) {
//         return sendCachedDownload(url);
//     }
//     if (item->ambiguous || item->size < 0) {
//         if (const auto result = cacheRemoteFile(url, *location, *item); !result.success()) {
//             return result;
//         }
//         if (wasKilled()) {
//             return KIO::WorkerResult::pass();
//         }
//         return sendCachedDownload(url);
//     }
//     totalSize(item->size);

//     const QString fileName = location->leafName();
//     if (fileName.contains(QLatin1Char('\n'))) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }
//     const QString parentSpec = location->parentSpec();

//     QProcess process;
//     RcloneProcess::configureProcess(process,
//                      m_rclone.executable(),
//                      {
//                          QStringLiteral("cat"),
//                          parentSpec,
//                          QStringLiteral("--files-from-raw"),
//                          QStringLiteral("-"),
//                          QStringLiteral("--buffer-size"),
//                          QStringLiteral("0"),
//                          QStringLiteral("--multi-thread-streams"),
//                          QStringLiteral("0"),
//                          QStringLiteral("--log-level"),
//                          QStringLiteral("ERROR"),
//                      });
//     process.setReadChannel(QProcess::StandardOutput);
//     process.start();
//     if (!process.waitForStarted(5000)) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS, process.errorString());
//     }
//     const QByteArray requestedFile = fileName.toUtf8() + '\n';
//     if (process.write(requestedFile) != requestedFile.size() || !process.waitForBytesWritten(5000)) {
//         RcloneProcess::stopProcess(process);
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
//     }
//     process.closeWriteChannel();

//     QByteArray standardError;
//     qint64 processed = 0;
//     while (process.state() != QProcess::NotRunning || process.bytesAvailable() > 0) {
//         if (wasKilled()) {
//             RcloneProcess::stopProcess(process);
//             return KIO::WorkerResult::pass();
//         }

//         if (process.bytesAvailable() == 0) {
//             process.waitForReadyRead(100);
//         }
//         RcloneProcess::appendLimited(standardError, process.readAllStandardError());

//         while (process.bytesAvailable() > 0) {
//             const QByteArray chunk = process.read(qMin<qint64>(DownloadChunkSize, process.bytesAvailable()));
//             if (chunk.isEmpty()) {
//                 break;
//             }
//             data(chunk);
//             processed += chunk.size();
//             processedSize(processed);
//         }
//     }
//     RcloneProcess::appendLimited(standardError, process.readAllStandardError());

//     if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
//         return errorResult(QString::fromUtf8(standardError).trimmed(), KIO::ERR_CANNOT_READ, url);
//     }
//     if (item->size >= 0 && processed != item->size) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, i18n("The remote file changed while it was being read."));
//     }

//     data(QByteArray());
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::put(const QUrl &url, int permissions, KIO::JobFlags flags)
// {
//     Q_UNUSED(permissions)

//     const RcloneUrl rcloneUrl(url);

//     if (!rcloneUrl.isValid()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }

//     if (rcloneUrl.isConfigureEntry()) {
//         return configurationEntryMutationError(KIO::ERR_WRITE_ACCESS_DENIED);
//     }

//     if (rcloneUrl.isRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, url.toDisplayString());
//     }

//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_CANNOT_WRITE, url);
//     }
//     if (!location->hasRcloneTarget() || location->isPhysicalRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("This virtual folder cannot be modified."));
//     }
//     if (!location->canWrite()) {
//         return KIO::WorkerResult::fail(KIO::ERR_WRITE_ACCESS_DENIED,
//                                        i18n("This Google Drive view is read-only. Open My Drive or a Shared Drive to make changes."));
//     }
//     if (const auto result = ensureUnambiguousParentDirectories(*location, KIO::ERR_CANNOT_WRITE); !result.success()) {
//         return result;
//     }
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (m_cachedDownloadSpec == location->rcloneSpec()) {
//         clearCachedDownload();
//     }

//     QString destinationError;
//     const std::optional<RcloneItem> expectedDestination = sourceItem(*location, &destinationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!expectedDestination && !destinationError.isEmpty() && !RcloneClient::isNotFoundError(destinationError)) {
//         return errorResult(destinationError, KIO::ERR_CANNOT_STAT, url);
//     }
//     if (expectedDestination) {
//         if (expectedDestination->isDirectory) {
//             return KIO::WorkerResult::fail(KIO::ERR_DIR_ALREADY_EXIST, url.toDisplayString());
//         }
//         if (expectedDestination->ambiguous) {
//             return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE,
//                                            i18n("Multiple remote objects have this name. Rename or remove the duplicates before editing this file."));
//         }
//         if (expectedDestination->readOnly) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_WRITE_ACCESS_DENIED,
//                 i18n("This is an exported cloud document with no stable remote size. It is available read-only to avoid replacing the original."));
//         }
//         if (!(flags & KIO::Overwrite)) {
//             return KIO::WorkerResult::fail(KIO::ERR_FILE_ALREADY_EXIST, url.toDisplayString());
//         }
//     }

//     bool sizeOk = false;
//     const qint64 sourceSize = metaData(QStringLiteral("sourceSize")).toLongLong(&sizeOk);
//     if (sizeOk && sourceSize >= 0) {
//         totalSize(sourceSize);
//     }

//     const QString suffix = QFileInfo(location->relativePath()).completeSuffix();
//     const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
//     QTemporaryFile localUpload(QDir::tempPath() + QStringLiteral("/kio-rclone-upload-XXXXXX") + extension);
//     if (!localUpload.open()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_OPEN_FOR_WRITING, localUpload.errorString());
//     }

//     qint64 processed = 0;
//     infoMessage(i18n("Preparing upload…"));
//     for (;;) {
//         if (wasKilled()) {
//             return KIO::WorkerResult::pass();
//         }

//         dataReq();
//         QByteArray sourceData;
//         const int readResult = readData(sourceData);
//         if (readResult < 0) {
//             return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
//         }
//         if (readResult == 0) {
//             break;
//         }
//         if (sizeOk && sourceSize >= 0 && processed > sourceSize - sourceData.size()) {
//             return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("The upload source changed size while it was being read."));
//         }
//         if (localUpload.write(sourceData) != sourceData.size()) {
//             return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, localUpload.errorString());
//         }
//         processed += sourceData.size();
//     }

//     if (sizeOk && sourceSize >= 0 && processed != sourceSize) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("The upload source changed size while it was being read."));
//     }
//     if (!localUpload.flush()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, localUpload.errorString());
//     }
//     const QString localUploadPath = localUpload.fileName();
//     localUpload.close();

//     const auto destinationChanged = [&expectedDestination](const std::optional<RcloneItem> &current) {
//         return expectedDestination.has_value() != current.has_value()
//             || (expectedDestination && current && (current->ambiguous || RcloneEntryFormat::itemVersion(*expectedDestination) != RcloneEntryFormat::itemVersion(*current)));
//     };
//     const auto currentDestination = [this, &location](QString *error) {
//         return sourceItem(*location, error);
//     };

//     QString currentError;
//     std::optional<RcloneItem> current = currentDestination(&currentError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!current && !currentError.isEmpty() && !RcloneClient::isNotFoundError(currentError)) {
//         return errorResult(currentError, KIO::ERR_CANNOT_STAT, url);
//     }
//     if (destinationChanged(current)) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, i18n("The remote file changed while the local edit was being prepared."));
//     }

//     const RcloneResult upload = runUpload(localUploadPath, location->rcloneSpec());
//     if (!upload.success()) {
//         return commandResult(upload, KIO::ERR_CANNOT_WRITE, url);
//     }

//     invalidateDirectorySnapshots();
//     if (expectedDestination) {
//         KioDirectoryNotifier::filesChanged({url});
//     } else {
//         KioDirectoryNotifier::filesAdded(parentDirectoryUrl(url));
//     }
//     processedSize(sizeOk && sourceSize >= 0 ? sourceSize : processed);
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url, int permissions)
// {
//     Q_UNUSED(permissions)

//     const RcloneUrl rcloneUrl(url);

//     if (!rcloneUrl.isValid()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }

//     if (rcloneUrl.isConfigureEntry()) {
//         return configurationEntryMutationError(KIO::ERR_FILE_ALREADY_EXIST);
//     }

//     if (rcloneUrl.isRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_MKDIR, url.toDisplayString());
//     }

//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_CANNOT_MKDIR, url);
//     }
//     if (!location->hasRcloneTarget() || location->isPhysicalRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_MKDIR, i18n("This virtual folder cannot be modified."));
//     }
//     if (!location->canWrite()) {
//         return KIO::WorkerResult::fail(KIO::ERR_WRITE_ACCESS_DENIED,
//                                        i18n("This Google Drive view is read-only. Open My Drive or a Shared Drive to make changes."));
//     }
//     if (const auto result = ensureUnambiguousParentDirectories(*location, KIO::ERR_CANNOT_MKDIR); !result.success()) {
//         return result;
//     }
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }

//     QString error;
//     const std::optional<RcloneItem> existingItem = sourceItem(*location, &error);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (existingItem) {
//         if (existingItem->ambiguous) {
//             return KIO::WorkerResult::fail(KIO::ERR_CANNOT_MKDIR,
//                                            i18n("Multiple remote objects have this name. Resolve the duplicates before creating this path."));
//         }
//         return KIO::WorkerResult::fail(existingItem->isDirectory ? KIO::ERR_DIR_ALREADY_EXIST : KIO::ERR_FILE_ALREADY_EXIST,
//                                        url.toDisplayString());
//     }
//     if (!error.isEmpty() && !RcloneClient::isNotFoundError(error)) {
//         return errorResult(error, KIO::ERR_CANNOT_STAT, url);
//     }

//     const RcloneResult result = runCommand({QStringLiteral("mkdir"), location->rcloneSpec()});
//     if (!result.success()) {
//         return commandResult(result, KIO::ERR_CANNOT_MKDIR, url);
//     }

//     invalidateDirectorySnapshots();
//     KioDirectoryNotifier::filesAdded(parentDirectoryUrl(url));
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags)
// {
//     clearCachedDownload();

//     const RcloneUrl source(src);
//     const RcloneUrl destination(dest);

//     if (!source.isValid() || !destination.isValid()) {
//         const QUrl &invalidUrl = !source.isValid() ? src : dest;
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, invalidUrl.toDisplayString());
//     }
//     if (source.isConfigureEntry() || destination.isConfigureEntry()) {
//         return configurationEntryMutationError(KIO::ERR_CANNOT_RENAME);
//     }
//     if (source.isRoot() || destination.isRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME, src.toDisplayString());
//     }
//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString sourceLocationError;
//     const std::optional<RcloneLocation> sourceLocation = resolveLocation(source, &sourceLocationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!sourceLocation) {
//         return errorResult(sourceLocationError, KIO::ERR_CANNOT_RENAME, src);
//     }

//     QString destinationLocationError;
//     const std::optional<RcloneLocation> destinationLocation = resolveLocation(destination, &destinationLocationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!destinationLocation) {
//         return errorResult(destinationLocationError, KIO::ERR_CANNOT_RENAME, dest);
//     }
//     if (!sourceLocation->hasRcloneTarget() || !destinationLocation->hasRcloneTarget() || sourceLocation->isPhysicalRoot()
//         || destinationLocation->isPhysicalRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME, i18n("A virtual folder cannot be renamed."));
//     }
//     if (!sourceLocation->canWrite() || !destinationLocation->canWrite()) {
//         return KIO::WorkerResult::fail(KIO::ERR_WRITE_ACCESS_DENIED,
//                                        i18n("This Google Drive view is read-only. Open My Drive or a Shared Drive to make changes."));
//     }
//     if (!sourceLocation->sharesMutationScope(*destinationLocation)) {
//         return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION,
//                                        i18n("Moving between different rclone remotes or Google Drive views is streamed by KIO."));
//     }
//     if (sourceLocation->rcloneSpec() == destinationLocation->rcloneSpec()) {
//         return KIO::WorkerResult::fail(KIO::ERR_IDENTICAL_FILES, src.toDisplayString());
//     }
//     if (const auto result = ensureUnambiguousParentDirectories(*sourceLocation, KIO::ERR_CANNOT_RENAME); !result.success()) {
//         return result;
//     }
//     if (const auto result = ensureUnambiguousParentDirectories(*destinationLocation, KIO::ERR_CANNOT_RENAME); !result.success()) {
//         return result;
//     }
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }

//     QString sourceError;
//     const std::optional<RcloneItem> sourceItemResult = sourceItem(*sourceLocation, &sourceError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!sourceItemResult) {
//         return errorResult(sourceError, KIO::ERR_CANNOT_RENAME, src);
//     }
//     if (sourceItemResult->ambiguous) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME,
//                                        i18n("Multiple remote objects have the source name. Resolve the duplicates before renaming this path."));
//     }

//     QString destinationError;
//     const std::optional<RcloneItem> destinationItem = sourceItem(*destinationLocation, &destinationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!destinationItem && !destinationError.isEmpty() && !RcloneClient::isNotFoundError(destinationError)) {
//         return errorResult(destinationError, KIO::ERR_CANNOT_STAT, dest);
//     }
//     if (destinationItem) {
//         if (destinationItem->ambiguous || destinationItem->readOnly) {
//             return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RENAME, i18n("The destination is ambiguous or read-only and cannot be replaced safely."));
//         }
//         if (!(flags & KIO::Overwrite)) {
//             return KIO::WorkerResult::fail(destinationItem->isDirectory ? KIO::ERR_DIR_ALREADY_EXIST : KIO::ERR_FILE_ALREADY_EXIST, dest.toDisplayString());
//         }
//     }

//     const RcloneResult result = runCommand(
//         {QStringLiteral("moveto"), sourceLocation->rcloneSpec(), destinationLocation->rcloneSpec(), QStringLiteral("--ignore-times")});
//     if (!result.success()) {
//         return commandResult(result, KIO::ERR_CANNOT_RENAME, src);
//     }

//     invalidateDirectorySnapshots();
//     KioDirectoryNotifier::fileRenamed(src, dest);
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags)
// {
//     Q_UNUSED(permissions)
//     Q_UNUSED(flags)

//     const RcloneUrl source(src);
//     const RcloneUrl destination(dest);

//     if (!source.isValid() || !destination.isValid()) {
//         const QUrl &invalidUrl = !source.isValid() ? src : dest;
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, invalidUrl.toDisplayString());
//     }

//     if (source.isConfigureEntry()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, i18n("“Configure Remotes…” is a virtual launcher and cannot be copied."));
//     }

//     if (destination.isConfigureEntry()) {
//         return configurationEntryMutationError(KIO::ERR_WRITE_ACCESS_DENIED);
//     }

//     // KIO's get + put fallback preserves pause/cancel backpressure. A direct
//     // rclone copy process would continue independently while Dolphin is paused.
//     return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION);
// }

// KIO::WorkerResult RcloneWorker::del(const QUrl &url, bool isFile)
// {
//     clearCachedDownload();

//     const RcloneUrl rcloneUrl(url);

//     if (!rcloneUrl.isValid()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }
//     if (rcloneUrl.isConfigureEntry()) {
//         return configurationEntryMutationError(KIO::ERR_CANNOT_DELETE);
//     }
//     if (rcloneUrl.isRoot()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_DELETE, url.toDisplayString());
//     }

//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR, url);
//     }
//     if (!location->hasRcloneTarget() || location->isPhysicalRoot()) {
//         return KIO::WorkerResult::fail(isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR,
//                                        i18n("This virtual folder cannot be modified."));
//     }
//     if (!location->canWrite()) {
//         return KIO::WorkerResult::fail(KIO::ERR_WRITE_ACCESS_DENIED,
//                                        i18n("This Google Drive view is read-only. Open My Drive or a Shared Drive to make changes."));
//     }
//     if (const auto result = ensureUnambiguousParentDirectories(*location, isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR);
//         !result.success()) {
//         return result;
//     }
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }

//     QString sourceError;
//     const std::optional<RcloneItem> item = sourceItem(*location, &sourceError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!item) {
//         return errorResult(sourceError, isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR, url);
//     }
//     if (item->ambiguous) {
//         return KIO::WorkerResult::fail(isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR,
//                                        i18n("Multiple remote objects have this name. Resolve the duplicates before deleting this path."));
//     }

//     QString command;
//     if (isFile) {
//         command = QStringLiteral("deletefile");
//     } else if (metaData(QStringLiteral("recurse")) == QLatin1String("true")) {
//         command = QStringLiteral("purge");
//     } else {
//         command = QStringLiteral("rmdir");
//     }

//     const RcloneResult result = runCommand({command, location->rcloneSpec()});
//     if (!result.success()) {
//         return commandResult(result, isFile ? KIO::ERR_CANNOT_DELETE : KIO::ERR_CANNOT_RMDIR, url);
//     }

//     invalidateDirectorySnapshots();
//     KioDirectoryNotifier::filesRemoved({url});
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::fileSystemFreeSpace(const QUrl &url)
// {
//     const RcloneUrl rcloneUrl(url);
//     if (!rcloneUrl.isValid() || rcloneUrl.remote().isEmpty() || rcloneUrl.isConfigureEntry()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_STAT, url.toDisplayString());
//     }
//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(rcloneUrl, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_CANNOT_STAT, url);
//     }

//     QString error;
//     const QString remoteSpec = location->hasRcloneTarget() ? location->rcloneRootSpec() : location->remote() + QLatin1Char(':');
//     const auto space = m_rclone.about(remoteSpec, &error, [this]() {
//         return wasKilled();
//     });
//     if (!space) {
//         return errorResult(error, KIO::ERR_UNSUPPORTED_ACTION, url);
//     }
//     if (space->total >= 0) {
//         setMetaData(QStringLiteral("total"), QString::number(space->total));
//     }
//     if (space->free >= 0) {
//         setMetaData(QStringLiteral("available"), QString::number(space->free));
//     }
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::ensureRcloneClient() const
// {
//     if (m_rclone.isAvailable()) {
//         return KIO::WorkerResult::pass();
//     }
//     return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS,
//                                    i18n("rclone was not found. Install it or set KIO_RCLONE_EXECUTABLE to "
//                                         "its full path."));
// }

// KIO::WorkerResult RcloneWorker::commandResult(const RcloneResult &result, int fallbackError, const QUrl &url) const
// {
//     if (result.cancelled || wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (result.success()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!result.started) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LAUNCH_PROCESS, result.errorMessage());
//     }
//     return errorResult(result.errorMessage(), fallbackError, url);
// }

// KIO::WorkerResult RcloneWorker::errorResult(const QString &message, int fallbackError, const QUrl &url) const
// {
//     const QString lowered = message.toLower();
//     if (RcloneClient::isNotFoundError(message)) {
//         return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
//     }
//     if (lowered.contains(QStringLiteral("permission denied")) || lowered.contains(QStringLiteral("access denied"))
//         || lowered.contains(QStringLiteral("unauthorized")) || lowered.contains(QStringLiteral("invalid_grant"))) {
//         return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, url.toDisplayString());
//     }
//     if (lowered.contains(QStringLiteral("didn't find section in config file")) || lowered.contains(QStringLiteral("config file not found"))) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
//     }

//     const QString usefulMessage = message.isEmpty() ? url.toDisplayString() : message;
//     return KIO::WorkerResult::fail(fallbackError, usefulMessage);
// }

// RcloneResult RcloneWorker::runCommand(const QStringList &arguments, int timeoutMs) const
// {
//     return m_rclone.run(arguments, timeoutMs, [this]() {
//         return wasKilled();
//     });
// }

// RcloneResult RcloneWorker::runUpload(const QString &localPath, const QString &remoteSpec)
// {
//     RcloneResult result;
//     QProcess process;
//     RcloneProcess::configureProcess(process,
//                      m_rclone.executable(),
//                      {
//                          QStringLiteral("copyto"),
//                          localPath,
//                          remoteSpec,
//                          QStringLiteral("--ignore-times"),
//                          QStringLiteral("--stats"),
//                          QStringLiteral("500ms"),
//                          QStringLiteral("--stats-one-line"),
//                          QStringLiteral("--stats-log-level"),
//                          QStringLiteral("NOTICE"),
//                          QStringLiteral("--use-json-log"),
//                          QStringLiteral("--log-level"),
//                          QStringLiteral("NOTICE"),
//                      });
//     process.start();
//     result.started = process.waitForStarted(5000);
//     if (!result.started) {
//         result.standardError = process.errorString().toUtf8();
//         return result;
//     }

//     QByteArray pendingLog;
//     qint64 remoteProcessed = 0;
//     const auto handleLogLine = [this, &result, &remoteProcessed](const QByteArray &line) {
//         QJsonParseError parseError;
//         const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
//         if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
//             RcloneProcess::appendLimited(result.standardError, line + '\n');
//             return;
//         }

//         const QJsonObject object = document.object();
//         const QJsonObject stats = object.value(QStringLiteral("stats")).toObject();
//         if (!stats.isEmpty()) {
//             qint64 transferred = stats.value(QStringLiteral("bytes")).toVariant().toLongLong();
//             double transferSpeed = stats.value(QStringLiteral("speed")).toDouble();
//             int percentage = -1;
//             qint64 eta = -1;

//             const QJsonArray transferring = stats.value(QStringLiteral("transferring")).toArray();
//             if (!transferring.isEmpty()) {
//                 const QJsonObject transfer = transferring.at(0).toObject();
//                 transferred = transfer.value(QStringLiteral("bytes")).toVariant().toLongLong();
//                 transferSpeed = transfer.value(QStringLiteral("speedAvg")).toDouble();
//                 if (transferSpeed <= 0) {
//                     transferSpeed = transfer.value(QStringLiteral("speed")).toDouble();
//                 }
//                 percentage = transfer.value(QStringLiteral("percentage")).toInt(-1);
//                 const QJsonValue etaValue = transfer.value(QStringLiteral("eta"));
//                 if (etaValue.isDouble()) {
//                     eta = qRound64(etaValue.toDouble());
//                 }
//             } else {
//                 const qint64 total = stats.value(QStringLiteral("totalBytes")).toVariant().toLongLong();
//                 if (total > 0) {
//                     percentage = qBound(0, int((transferred * 100) / total), 100);
//                 }
//                 const QJsonValue etaValue = stats.value(QStringLiteral("eta"));
//                 if (etaValue.isDouble()) {
//                     eta = qRound64(etaValue.toDouble());
//                 }
//             }

//             remoteProcessed = qMax(remoteProcessed, transferred);
//             processedSize(remoteProcessed);
//             if (transferSpeed > 0) {
//                 speed(static_cast<unsigned long>(qMin(transferSpeed, double(std::numeric_limits<unsigned long>::max()))));
//             }

//             const QString formattedSpeed = QLocale().formattedDataSize(qMax<qint64>(0, qRound64(transferSpeed)));
//             if (percentage >= 0 && eta >= 0) {
//                 infoMessage(i18n("Uploading… %1% · %2/s · %3 s remaining", percentage, formattedSpeed, eta));
//             } else if (percentage >= 0) {
//                 infoMessage(i18n("Uploading… %1% · %2/s", percentage, formattedSpeed));
//             }
//             return;
//         }

//         const QString message = object.value(QStringLiteral("msg")).toString().trimmed();
//         if (!message.isEmpty()) {
//             RcloneProcess::appendLimited(result.standardError, message.toUtf8() + '\n');
//         }
//     };
//     const auto collectOutput = [&process, &result, &pendingLog, &handleLogLine](bool flush) {
//         RcloneProcess::appendLimited(result.standardOutput, process.readAllStandardOutput());
//         pendingLog.append(process.readAllStandardError());
//         for (;;) {
//             const qsizetype newline = pendingLog.indexOf('\n');
//             if (newline < 0) {
//                 break;
//             }
//             handleLogLine(pendingLog.left(newline));
//             pendingLog.remove(0, newline + 1);
//         }
//         if (flush && !pendingLog.isEmpty()) {
//             handleLogLine(pendingLog);
//             pendingLog.clear();
//         }
//     };

//     infoMessage(i18n("Uploading…"));
//     while (process.state() != QProcess::NotRunning) {
//         if (wasKilled()) {
//             result.cancelled = true;
//             RcloneProcess::stopProcess(process);
//             break;
//         }
//         process.waitForFinished(100);
//         collectOutput(false);
//     }
//     collectOutput(true);
//     result.exitCode = process.exitCode();
//     return result;
// }

// std::optional<bool> RcloneWorker::remoteMayHaveDuplicateNames(const RcloneLocation &location)
// {
//     const QString remoteSpec = location.rcloneRootSpec();
//     if (remoteSpec.isEmpty()) {
//         return std::nullopt;
//     }

//     const auto cached = m_remoteDuplicateNameSupport.constFind(remoteSpec);
//     if (cached != m_remoteDuplicateNameSupport.cend()) {
//         return *cached;
//     }

//     QString ignoredError;
//     const std::optional<bool> support = m_rclone.mayHaveDuplicateNames(
//         remoteSpec,
//         &ignoredError,
//         [this]() {
//             return wasKilled();
//         });
//     if (support && !wasKilled()) {
//         m_remoteDuplicateNameSupport.insert(remoteSpec, *support);
//     }
//     return support;
// }

// KIO::WorkerResult RcloneWorker::ensureUnambiguousParentDirectories(const RcloneLocation &location, int fallbackError) const
// {
//     const QStringList parts = location.relativePath().split(QLatin1Char('/'), Qt::SkipEmptyParts);
//     QString parentPath;
//     for (qsizetype index = 0; index + 1 < parts.size(); ++index) {
//         parentPath += (parentPath.isEmpty() ? QString() : QStringLiteral("/")) + parts.at(index);

//         QString error;
//         const std::optional<RcloneItem> parent = sourceItem(location.withRelativePath(parentPath), &error);
//         if (wasKilled()) {
//             return KIO::WorkerResult::pass();
//         }
//         if (!parent) {
//             return KIO::WorkerResult::fail(fallbackError,
//                                            error.isEmpty() ? i18n("The parent directory could not be resolved safely.") : error);
//         }
//         if (!parent->isDirectory) {
//             return KIO::WorkerResult::fail(fallbackError, i18n("A parent path component is not a directory."));
//         }
//         if (parent->ambiguous) {
//             return KIO::WorkerResult::fail(fallbackError,
//                                            i18n("A parent directory has multiple remote objects with the same name. "
//                                                 "Resolve the duplicates before changing this path."));
//         }
//     }
//     return KIO::WorkerResult::pass();
// }

// std::optional<RcloneItem> RcloneWorker::sourceItem(const RcloneLocation &location, QString *error) const
// {
//     if (!location.hasRcloneTarget() || location.relativePath().isEmpty()) {
//         if (error) {
//             *error = QStringLiteral("object not found");
//         }
//         return std::nullopt;
//     }

//     const QString fileName = location.leafName();
//     const QString parentSpec = location.parentSpec();

//     QString listError;
//     std::optional<RcloneItem> selected;
//     bool ambiguous = false;
//     const bool listed = m_rclone.listStreaming(
//         parentSpec,
//         [&fileName, &selected, &ambiguous](const RcloneItem &item) {
//             if (item.name != fileName) {
//                 return true;
//             }
//             if (selected) {
//                 ambiguous = true;
//                 if (RcloneEntryFormat::preferItem(item, *selected)) {
//                     selected = item;
//                 }
//                 return true;
//             }
//             selected = item;
//             return true;
//         },
//         &listError,
//         [this]() {
//             return wasKilled();
//         });
//     if (!listed) {
//         if (error) {
//             *error = listError;
//         }
//         return std::nullopt;
//     }

//     if (!selected) {
//         if (error) {
//             *error = QStringLiteral("object not found");
//         }
//         return std::nullopt;
//     }
//     selected->ambiguous = ambiguous;
//     selected->readOnly = selected->readOnly || selected->ambiguous;
//     return selected;
// }

// std::optional<RcloneItem> RcloneWorker::cachedItemForReadOnlyRequest(const RcloneLocation &location)
// {
//     if (!location.hasRcloneTarget() || location.relativePath().isEmpty()) {
//         return std::nullopt;
//     }

//     if (const auto syntheticItem = rememberedSyntheticTrashItem(location)) {
//         return syntheticItem;
//     }

//     const DirectoryListingPolicy policy = DirectoryListingPolicyStore::load();
//     if (!policy.allowsSnapshots()) {
//         return std::nullopt;
//     }

//     const QString fileName = location.leafName();
//     const QString parentPath = location.relativePath().section(QLatin1Char('/'), 0, -2);
//     const RcloneLocation parent = location.withRelativePath(parentPath);
//     const auto items = m_directorySnapshots.load(location.remote(), parent.cachePath(), policy.freshnessSeconds);
//     if (!items) {
//         return std::nullopt;
//     }

//     const auto item = std::find_if(items->cbegin(), items->cend(), [&fileName](const RcloneItem &candidate) {
//         return candidate.name == fileName;
//     });
//     if (item == items->cend()) {
//         return std::nullopt;
//     }
//     return *item;
// }

// std::optional<RcloneItem> RcloneWorker::rememberedSyntheticTrashItem(const RcloneLocation &location) const
// {
//     if (!location.isDriveTrash() || location.relativePath().isEmpty()) {
//         return std::nullopt;
//     }
//     const auto item = m_syntheticTrashItems.constFind(syntheticTrashItemKey(location.remote(), location.relativePath()));
//     return item == m_syntheticTrashItems.cend() ? std::nullopt : std::optional<RcloneItem>(*item);
// }

// void RcloneWorker::rememberSyntheticTrashItems(const RcloneLocation &root, const RcloneRecursiveListing &listing)
// {
//     const QString prefix = root.remote() + QLatin1Char('\n');
//     for (auto item = m_syntheticTrashItems.begin(); item != m_syntheticTrashItems.end();) {
//         item = item.key().startsWith(prefix) ? m_syntheticTrashItems.erase(item) : std::next(item);
//     }

//     for (auto directory = listing.directories().cbegin(); directory != listing.directories().cend(); ++directory) {
//         for (const RcloneItem &item : directory.value()) {
//             if (!item.syntheticDirectory) {
//                 continue;
//             }
//             const QString relativePath = directory.key().isEmpty() ? item.name : directory.key() + QLatin1Char('/') + item.name;
//             m_syntheticTrashItems.insert(syntheticTrashItemKey(root.remote(), relativePath), item);
//         }
//     }
// }

// void RcloneWorker::invalidateDirectorySnapshots()
// {
//     m_directorySnapshots.clear();
//     m_syntheticTrashItems.clear();
// }

// KIO::WorkerResult RcloneWorker::cacheRemoteFile(const QUrl &url, const RcloneLocation &location, RcloneItem &item)
// {
//     if (cachedDownloadMatches(location, item)) {
//         item.size = m_cachedDownload->size();
//         return KIO::WorkerResult::pass();
//     }

//     const QString remoteVersion = RcloneEntryFormat::itemVersion(item);
//     clearCachedDownload();
//     if (item.ambiguous && item.id.isEmpty()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ,
//                                        i18n("Multiple remote objects have this name, and this backend cannot identify which one to open safely."));
//     }

//     const QString suffix = QFileInfo(location.relativePath()).completeSuffix();
//     const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
//     const QString fileTemplate = QDir::tempPath() + QStringLiteral("/kio-rclone-XXXXXX") + extension;
//     auto temporaryFile = std::make_unique<QTemporaryFile>(fileTemplate);
//     if (!temporaryFile->open()) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_OPEN_FOR_WRITING, temporaryFile->errorString());
//     }
//     const QString temporaryPath = temporaryFile->fileName();
//     temporaryFile->close();

//     RcloneResult result;
//     if (!item.id.isEmpty()) {
//         result = m_rclone.run({QStringLiteral("backend"),
//                                 QStringLiteral("copyid"),
//                                 location.rcloneRootSpec(),
//                                 item.id,
//                                 temporaryPath,
//                                 QStringLiteral("--ignore-times"),
//                                 QStringLiteral("--log-level"),
//                                 QStringLiteral("ERROR")},
//                                -1,
//                                [this]() {
//                                    return wasKilled();
//                                });
//     }

//     if (item.id.isEmpty() || !result.success()) {
//         if (result.cancelled || wasKilled()) {
//             return KIO::WorkerResult::pass();
//         }
//         if (item.ambiguous && !item.id.isEmpty()) {
//             return errorResult(result.errorMessage(), KIO::ERR_CANNOT_READ, url);
//         }

//         QFile::remove(temporaryPath);
//         result = m_rclone.run({QStringLiteral("copyto"),
//                                 location.rcloneSpec(),
//                                 temporaryPath,
//                                 QStringLiteral("--ignore-times"),
//                                 QStringLiteral("--log-level"),
//                                 QStringLiteral("ERROR")},
//                                -1,
//                                [this]() {
//                                    return wasKilled();
//                                });
//     }

//     if (result.cancelled || wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!result.success()) {
//         return errorResult(result.errorMessage(), KIO::ERR_CANNOT_READ, url);
//     }
//     if (!temporaryFile->open() || !temporaryFile->seek(0)) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, temporaryFile->errorString());
//     }

//     item.size = temporaryFile->size();
//     m_cachedDownloadSpec = location.rcloneSpec();
//     m_cachedDownloadVersion = remoteVersion;
//     m_cachedDownload = std::move(temporaryFile);
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::resolveUnknownSize(const QUrl &url, const RcloneLocation &location, RcloneItem &item)
// {
//     return cacheRemoteFile(url, location, item);
// }

// bool RcloneWorker::cachedDownloadMatches(const RcloneLocation &location, const RcloneItem &item) const
// {
//     return m_cachedDownload && m_cachedDownloadSpec == location.rcloneSpec()
//         && m_cachedDownloadVersion == RcloneEntryFormat::itemVersion(item);
// }

// KIO::WorkerResult RcloneWorker::sendCachedDownload(const QUrl &url)
// {
//     if (!m_cachedDownload || !m_cachedDownload->seek(0)) {
//         clearCachedDownload();
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, url.toDisplayString());
//     }

//     totalSize(m_cachedDownload->size());
//     qint64 processed = 0;
//     for (;;) {
//         if (wasKilled()) {
//             clearCachedDownload();
//             return KIO::WorkerResult::pass();
//         }

//         const QByteArray chunk = m_cachedDownload->read(DownloadChunkSize);
//         if (chunk.isEmpty()) {
//             if (m_cachedDownload->error() != QFileDevice::NoError) {
//                 const QString error = m_cachedDownload->errorString();
//                 clearCachedDownload();
//                 return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, error);
//             }
//             break;
//         }

//         data(chunk);
//         processed += chunk.size();
//         processedSize(processed);
//     }

//     data(QByteArray());
//     clearCachedDownload();
//     return KIO::WorkerResult::pass();
// }

// void RcloneWorker::clearCachedDownload()
// {
//     m_cachedDownload.reset();
//     m_cachedDownloadSpec.clear();
//     m_cachedDownloadVersion.clear();
// }

// #include "worker.moc"
