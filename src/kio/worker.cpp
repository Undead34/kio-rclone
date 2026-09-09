/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "worker.h"
#include "rclone/url.h"

#include <QDateTime>
#include <QUrl>

#include <KIO/UDSEntry>
#include <KIO/Global>
#include <KLocalizedString>

namespace {

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

void reserveEntry(KIO::UDSEntry &entry, int strings, int numbers)
{
#if KIO_VERSION >= QT_VERSION_CHECK(6, 29, 0)
    entry.reserveStrings(strings);
    entry.reserveNumbers(numbers);
#else
    entry.reserve(strings + numbers);
#endif
}

KIO::WorkerResult notImplemented(const QString &operation)
{
    return KIO::WorkerResult::fail(
        KIO::ERR_UNSUPPORTED_ACTION,
        operation + QStringLiteral(" not implemented"));
}

KIO::WorkerResult malformedUrlResult(const QUrl &url)
{
    return KIO::WorkerResult::fail(
        KIO::ERR_MALFORMED_URL,
        url.toDisplayString());
}

KIO::WorkerResult listFailure(const RcloneError &error,
                              const QUrl &url)
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
            KIO::ERR_CANNOT_ENTER_DIRECTORY,
            error.message);
    }

    return KIO::WorkerResult::fail(
        KIO::ERR_CANNOT_ENTER_DIRECTORY,
        display);
}

KIO::UDSEntry makeDirectoryEntry(const QString &name)
{
    KIO::UDSEntry entry;

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0700);

    return entry;
}

KIO::UDSEntry makeConfigLauncherEntry()
{
    KIO::UDSEntry entry;
    reserveEntry(entry, 5, 3);

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, RcloneUrl::ConfigureEntry);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Configure Remotes…"));
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("configure"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
    entry.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, RcloneUrl::ConfigureEntry);
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, RcloneUrl::ConfigurationLauncherMimeType);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0500);
    entry.fastInsert(KIO::UDSEntry::UDS_HIDDEN, 0);

    return entry;
}

KIO::UDSEntry makeEntry(const RcloneItem &item)
{
    KIO::UDSEntry entry;

    QString name = item.name;

    if (name.isEmpty()) {
        name = item.path.section(QLatin1Char('/'), -1);
    }

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);

    if (item.isDirectory) {
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, item.readOnly ? 0500 : 0700);
    } else {
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, item.readOnly ? 0400 : 0600);

        if (!item.mimeType.isEmpty()) {
            entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, item.mimeType);
        }

        if (item.size >= 0) {
            entry.fastInsert(KIO::UDSEntry::UDS_SIZE, item.size);
        }
    }

    if (item.modificationTime.isValid()) {
        entry.fastInsert(
            KIO::UDSEntry::UDS_MODIFICATION_TIME,
            item.modificationTime.toSecsSinceEpoch());
    }

    return entry;
}

KIO::UDSEntry makeRootEntry()
{
    KIO::UDSEntry entry;
    reserveEntry(entry, 4, 2);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Rclone Remotes"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-kio-rclone"));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

} // namespace

RcloneWorker::RcloneWorker(const QByteArray &protocol,
                           const QByteArray &poolSocket,
                           const QByteArray &appSocket)
    : KIO::WorkerBase(protocol, poolSocket, appSocket)
{
}

KIO::WorkerResult RcloneWorker::listDir(const QUrl &url)
{
    const auto rcloneUrl = RcloneUrl::parse(url);

    if (!rcloneUrl) {
        return malformedUrlResult(url);
    }

    if (rcloneUrl->isConfigureEntry()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_FILE,
            url.toDisplayString());
    }

    if (rcloneUrl->isRoot()) {
        return listRoot(url);
    }

    WorkerRcloneContext ctx(*this);

    const RcloneStatus status = m_client.list(
        rcloneUrl->toCliSpec(),
        false,
        [this](const RcloneItem &item) {
            listEntry(makeEntry(item));
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
    Q_UNUSED(url)

    // TODO: Obtener los metadatos del elemento.
    return notImplemented(QStringLiteral("stat"));
}

KIO::WorkerResult RcloneWorker::mimetype(const QUrl &url)
{
    Q_UNUSED(url)

    // TODO: Determinar el tipo MIME del elemento.
    return notImplemented(QStringLiteral("mimetype"));
}

KIO::WorkerResult RcloneWorker::get(const QUrl &url)
{
    Q_UNUSED(url)

    // TODO: Descargar el archivo y entregar sus datos a KIO.
    return notImplemented(QStringLiteral("get"));
}

KIO::WorkerResult RcloneWorker::put(const QUrl &url,
                                    int permissions,
                                    KIO::JobFlags flags)
{
    Q_UNUSED(url)
    Q_UNUSED(permissions)
    Q_UNUSED(flags)

    // TODO: Recibir los datos de KIO y subir el archivo.
    return notImplemented(QStringLiteral("put"));
}

KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url,
                                      int permissions)
{
    Q_UNUSED(url)
    Q_UNUSED(permissions)

    // TODO: Crear el directorio remoto.
    return notImplemented(QStringLiteral("mkdir"));
}

KIO::WorkerResult RcloneWorker::rename(const QUrl &src,
                                       const QUrl &dest,
                                       KIO::JobFlags flags)
{
    Q_UNUSED(src)
    Q_UNUSED(dest)
    Q_UNUSED(flags)

    // TODO: Mover o renombrar el elemento remoto.
    return notImplemented(QStringLiteral("rename"));
}

KIO::WorkerResult RcloneWorker::copy(const QUrl &src,
                                     const QUrl &dest,
                                     int permissions,
                                     KIO::JobFlags flags)
{
    Q_UNUSED(src)
    Q_UNUSED(dest)
    Q_UNUSED(permissions)
    Q_UNUSED(flags)

    // TODO: Copiar el elemento remoto.
    return notImplemented(QStringLiteral("copy"));
}

KIO::WorkerResult RcloneWorker::del(const QUrl &url,
                                    bool isFile)
{
    Q_UNUSED(url)
    Q_UNUSED(isFile)

    // TODO: Eliminar el archivo o directorio remoto.
    return notImplemented(QStringLiteral("del"));
}

KIO::WorkerResult RcloneWorker::fileSystemFreeSpace(const QUrl &url)
{
    Q_UNUSED(url)

    // TODO: Consultar las métricas de espacio del remoto.
    return notImplemented(QStringLiteral("fileSystemFreeSpace"));
}

/// Privates

KIO::WorkerResult RcloneWorker::listRoot(const QUrl &url)
{
    WorkerRcloneContext ctx(*this);

    const auto remotes = m_client.listRemotes(ctx);

    if (!remotes.success()) {
        return listFailure(remotes.error, url);
    }

    listEntry(makeRootEntry());
    listEntry(makeConfigLauncherEntry());

    for (const RcloneRemote &remote : *remotes.data) {
        if (ctx.isCancelled()) {
            return KIO::WorkerResult::fail(
                KIO::ERR_USER_CANCELED,
                url.toDisplayString());
        }

        listEntry(makeDirectoryEntry(remote.name));
    }

    return KIO::WorkerResult::pass();
}

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

// KIO::WorkerResult RcloneWorker::listDir(const QUrl &url)
// {
//     const auto directory = RcloneUrl::parse(url);

//     if (!directory.has_value()) {
//         return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url.toDisplayString());
//     }

//     if (directory->isConfigureEntry()) {
//         return KIO::WorkerResult::fail(KIO::ERR_IS_FILE, url.toDisplayString());
//     }

//     if (const auto result = ensureRcloneClient(); !result.success()) {
//         return result;
//     }

//     const bool reloadRequested = bypassesDirectorySnapshot(metaData(QStringLiteral("cache")));

//     if (directory.isRoot()) {
//         if (reloadRequested) {
//             invalidateDirectorySnapshots();
//         }
//         return listRoot(url);
//     }

//     QString locationError;
//     const std::optional<RcloneLocation> location = resolveLocation(directory, &locationError);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!location) {
//         return errorResult(locationError, KIO::ERR_MALFORMED_URL, url);
//     }

//     if (reloadRequested) {
//         // F5/reload deliberately has stronger freshness semantics. Clear the
//         // shared snapshots before doing any virtual or physical listing, so
//         // an unsuccessful refresh cannot revive data the user rejected.
//         invalidateDirectorySnapshots();
//     }
//     if (location->isDriveHub()) {
//         return listDriveHub(*location);
//     }
//     if (location->isSharedDrivesRoot()) {
//         return listSharedDrives(url, *location);
//     }
//     if (location->isFoldersRoot()) {
//         return listFoldersIndex(*location);
//     }

//     const DirectoryListingPolicy policy = DirectoryListingPolicyStore::load();
//     if (location->isDriveTrash()) {
//         return listDriveTrash(url, *location, policy);
//     }
//     if (!reloadRequested) {
//         if (const auto cachedItems = cachedDirectory(*location, policy)) {
//             publishDirectoryEntries(*location, *cachedItems);
//             return KIO::WorkerResult::pass();
//         }
//     }

//     return listRemoteDirectory(url, *location, policy);
// }

// KIO::WorkerResult RcloneWorker::listRoot(const QUrl &requestUrl)
// {
//     QString error;
//     const QList<RcloneRemote> remotes = m_rclone.remoteList(&error, [this]() {
//         return wasKilled();
//     });
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!error.isEmpty()) {
//         return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl);
//     }

//     // Current rclone versions report a backend type here. If an older version
//     // omits it, preserve the generic remote-path behavior instead of reading
//     // the full config or guessing a provider from its name.
//     m_remoteDuplicateNameSupport.clear();
//     m_remoteTypes.clear();
//     m_sharedDriveNames.clear();
//     listEntry(KioEntryBuilder::root());
//     for (const RcloneRemote &remote : remotes) {
//         m_remoteTypes.insert(remote.name, remote.type);
//         listEntry(KioEntryBuilder::remote(remote.name, false, remote.type));
//     }
//     listEntry(KioEntryBuilder::configure());
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::listDriveHub(const RcloneLocation &location)
// {
//     listEntry(currentDirectoryEntry(location));
//     for (const RcloneLocation::HubEntry &entry : RcloneLocation::driveHubEntries()) {
//         listEntry(KioEntryBuilder::directory(entry.name, driveDisplayName(entry.kind), entry.iconName, entry.writable));
//     }
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::listDriveTrash(const QUrl &requestUrl,
//                                                const RcloneLocation &directory,
//                                                const DirectoryListingPolicy &policy)
// {
//     if (const auto cachedItems = cachedDirectory(directory, policy)) {
//         publishDirectoryEntries(directory, *cachedItems);
//         return KIO::WorkerResult::pass();
//     }

//     // `--drive-trashed-only` can supply ordinary parent folders when listing
//     // one level at a time. Ask rclone for the whole filtered result once, then
//     // reconstruct only branches which lead to a trashed item. rclone documents
//     // --recursive for lsjson at https://rclone.org/commands/rclone_lsjson/.
//     QList<RcloneItem> recursiveItems;
//     QString error;
//     const bool listed = m_rclone.listStreaming(
//         directory.rcloneRootSpec(),
//         [&recursiveItems](const RcloneItem &item) {
//             recursiveItems.append(item);
//             return true;
//         },
//         &error,
//         [this]() {
//             return wasKilled();
//         },
//         RcloneListingDepth::Recursive);
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!listed) {
//         return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl);
//     }

//     const auto listing = RcloneRecursiveListing::fromItems(recursiveItems, &error);
//     if (!listing) {
//         return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl);
//     }

//     rememberSyntheticTrashItems(directory, *listing);
//     for (auto snapshot = listing->directories().cbegin(); snapshot != listing->directories().cend(); ++snapshot) {
//         if (!policy.allowsSnapshots()) {
//             break;
//         }
//         const RcloneLocation snapshotDirectory = directory.withRelativePath(snapshot.key());
//         static_cast<void>(m_directorySnapshots.store(snapshotDirectory.remote(), snapshotDirectory.cachePath(), snapshot.value()));
//     }

//     const auto entries = listing->entries(directory.relativePath());
//     if (!entries) {
//         return KIO::WorkerResult::fail(KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl.toDisplayString());
//     }
//     publishDirectoryEntries(directory, *entries);
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::listSharedDrives(const QUrl &requestUrl, const RcloneLocation &location)
// {
//     QString error;
//     const QList<RcloneSharedDrive> drives = m_rclone.sharedDrives(location.remote() + QLatin1Char(':'), &error, [this]() {
//         return wasKilled();
//     });
//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }
//     if (!error.isEmpty()) {
//         return errorResult(error, KIO::ERR_CANNOT_ENTER_DIRECTORY, requestUrl);
//     }

//     listEntry(currentDirectoryEntry(location));
//     for (const RcloneSharedDrive &drive : drives) {
//         m_sharedDriveNames.insert(location.remote() + QLatin1Char('\n') + drive.id, drive.name);
//         listEntry(KioEntryBuilder::directory(drive.id, drive.name, QStringLiteral("folder-cloud"), true));
//     }
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::listFoldersIndex(const RcloneLocation &location)
// {
//     // Folder IDs are not enumerable through rclone. This expert route exists
//     // for a known ID (for example a Drive URL), not as a misleading empty
//     // representation of the user's My Drive.
//     listEntry(currentDirectoryEntry(location));
//     return KIO::WorkerResult::pass();
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
