/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "worker.h"
#include "workerhelpers.h"
#include "rclone/url.h"

#include <KLocalizedString>

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
        return RcloneWorkerHelpers::malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        return KIO::WorkerResult::fail(
            KIO::ERR_IS_FILE,
            url.toDisplayString());
    }

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);

    const RcloneStatus status =
        location->isRoot()
            ? m_navigation.listRoot(
                  [this](const RcloneNavigationEntry &entry) {
                      listEntry(RcloneWorkerHelpers::makeEntry(entry));
                      return !wasKilled();
                  },
                  ctx)
            : m_navigation.list(
                  *location,
                  [this](const RcloneNavigationEntry &entry) {
                      listEntry(RcloneWorkerHelpers::makeEntry(entry));
                      return !wasKilled();
                  },
                  ctx);

    if (!status.success()) {
        return RcloneWorkerHelpers::listFailure(status.error, url);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::stat(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        statEntry(RcloneWorkerHelpers::makeEntry(RcloneNavigationEntry::virtualFile(
            RcloneUrl::ConfigureEntry,
            RcloneUrlBuilder::createConfigLauncher(),
            RcloneUrl::ConfigurationLauncherMimeType,
            QStringLiteral("configure"),
            i18n("Configure Remotes…"))));
        return KIO::WorkerResult::pass();
    }

    if (location->kind() == RcloneLocation::Kind::DriveSharedDrive
        && location->identifier().isEmpty()) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
    }

    if (RcloneWorkerHelpers::isSyntheticDirectory(*location)) {
        statEntry(RcloneWorkerHelpers::makeEntry(RcloneWorkerHelpers::virtualDirectoryForStat(*location, url)));
        return KIO::WorkerResult::pass();
    }

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const auto item = m_client.stat(
        location->toCliSpec(),
        RcloneWorkerHelpers::statOptionsFor(*location),
        ctx);

    if (!item.success()) {
        return RcloneWorkerHelpers::statFailure(item.error, url);
    }

    if (wasKilled()) {
        return KIO::WorkerResult::pass();
    }

    RcloneItem resolvedItem = *item.data;

    QUrl itemUrl = RcloneWorkerHelpers::withItemIdentity(url, resolvedItem);

    if (location->isDriveVirtual()) {
        itemUrl = RcloneWorkerHelpers::withRemoteType(itemUrl, QStringLiteral("drive"));
    }

    statEntry(RcloneWorkerHelpers::makeEntry(RcloneNavigationEntry::fromItem(
        resolvedItem,
        itemUrl)));

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult RcloneWorker::mimetype(const QUrl &url)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
    }

    if (location->isConfigureEntry()) {
        mimeType(RcloneUrl::ConfigurationLauncherMimeType);
        return KIO::WorkerResult::pass();
    }

    if (location->kind() == RcloneLocation::Kind::DriveSharedDrive
        && location->identifier().isEmpty()) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
    }

    if (RcloneWorkerHelpers::isSyntheticDirectory(*location)) {
        mimeType(QStringLiteral("inode/directory"));
        return KIO::WorkerResult::pass();
    }

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const auto item = m_client.stat(
        location->toCliSpec(),
        RcloneWorkerHelpers::statOptionsFor(*location),
        ctx);

    if (!item.success()) {
        return RcloneWorkerHelpers::rcloneFailure(item.error, url, KIO::ERR_DOES_NOT_EXIST);
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

// KIO::WorkerResult RcloneWorker::get(const QUrl &url)
// {
//     const auto location = RcloneLocation::parse(url);

//     if (!location) {
//         return malformedUrlResult(url);
//     }

//     if (location->isConfigureEntry()) {
//         redirection(RcloneUrlBuilder::createConfigLauncher());
//         return KIO::WorkerResult::pass();
//     }

//     if (isSyntheticDirectory(*location)) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_IS_DIRECTORY,
//             url.toDisplayString());
//     }

//     /*
//      * download() todavía no recibe flags por vista. No debemos omitirlos:
//      * hacerlo podría leer un homónimo de My Drive en vez del elemento que
//      * Dolphin muestra en la vista filtrada.
//      */
//     switch (location->kind()) {
//     case RcloneLocation::Kind::DriveSharedWithMe:
//     case RcloneLocation::Kind::DriveTrash:
//     case RcloneLocation::Kind::DriveStarred:
//     case RcloneLocation::Kind::DriveSharedDrive:
//         return KIO::WorkerResult::fail(
//             KIO::ERR_UNSUPPORTED_ACTION,
//             i18n("Reading files from this Google Drive view is not available yet."));

//     case RcloneLocation::Kind::DriveHub:
//     case RcloneLocation::Kind::DriveSharedDrives:
//         return KIO::WorkerResult::fail(
//             KIO::ERR_IS_DIRECTORY,
//             url.toDisplayString());

//     case RcloneLocation::Kind::Root:
//     case RcloneLocation::Kind::ConfigureEntry:
//     case RcloneLocation::Kind::Standard:
//     case RcloneLocation::Kind::DriveMyDrive:
//         break;
//     }

//     WorkerRcloneContext ctx(*this);
//     const auto item = m_client.stat(
//         location->toCliSpec(),
//         statOptionsFor(*location),
//         ctx);

//     if (!item.success()) {
//         return rcloneFailure(item.error, url, KIO::ERR_CANNOT_READ);
//     }

//     if (item.data->isDirectory) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_IS_DIRECTORY,
//             url.toDisplayString());
//     }

//     if (wasKilled()) {
//         return KIO::WorkerResult::pass();
//     }

//     RcloneItem resolvedItem = *item.data;

//     mimeType(resolvedItem.mimeType.isEmpty()
//                  ? QStringLiteral("application/octet-stream")
//                  : resolvedItem.mimeType);

//     if (resolvedItem.size >= 0) {
//         totalSize(resolvedItem.size);
//     }

//     qint64 processed = 0;
//     const RcloneStatus status = m_client.download(
//         location->toCliSpec(),
//         [this, &processed](const QByteArray &chunk) {
//             if (wasKilled()) {
//                 return false;
//             }

//             data(chunk);
//             processed += chunk.size();
//             processedSize(processed);
//             return true;
//         },
//         ctx);

//     if (!status.success()) {
//         return rcloneFailure(status.error, url, KIO::ERR_CANNOT_READ);
//     }

//     if (resolvedItem.size >= 0 && processed != resolvedItem.size) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_READ,
//             i18n("The remote file changed while it was being read."));
//     }

//     data(QByteArray());
//     return KIO::WorkerResult::pass();
// }

// KIO::WorkerResult RcloneWorker::put(const QUrl &url,
//                                     int permissions,
//                                     KIO::JobFlags flags)
// {
//     Q_UNUSED(permissions)

//     const auto location = RcloneLocation::parse(url);

//     if (!location) {
//         return malformedUrlResult(url);
//     }

//     if (location->isRoot()
//         || location->isConfigureEntry()
//         || location->remotePath().isEmpty()) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_WRITE,
//             url.toDisplayString());
//     }

//     switch (location->kind()) {
//     case RcloneLocation::Kind::DriveSharedWithMe:
//     case RcloneLocation::Kind::DriveTrash:
//     case RcloneLocation::Kind::DriveStarred:
//         return KIO::WorkerResult::fail(
//             KIO::ERR_WRITE_ACCESS_DENIED,
//             i18n("This Google Drive view is read-only."));

//     case RcloneLocation::Kind::DriveHub:
//     case RcloneLocation::Kind::DriveSharedDrives:
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_WRITE,
//             url.toDisplayString());

//     case RcloneLocation::Kind::DriveSharedDrive:
//         return KIO::WorkerResult::fail(
//             KIO::ERR_UNSUPPORTED_ACTION,
//             i18n("Uploading files to Shared Drives is not available yet."));

//     case RcloneLocation::Kind::Root:
//     case RcloneLocation::Kind::ConfigureEntry:
//     case RcloneLocation::Kind::Standard:
//     case RcloneLocation::Kind::DriveMyDrive:
//         break;
//     }

//     WorkerRcloneContext ctx(*this);
//     const auto destination = m_client.stat(
//         location->toCliSpec(),
//         statOptionsFor(*location),
//         ctx);
//     RcloneTargetSnapshot targetSnapshot;

//     if (destination.success()) {
//         targetSnapshot =
//             RcloneTargetSnapshot::fromItem(*destination.data);

//         if (destination.data->isDirectory) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_DIR_ALREADY_EXIST,
//                 url.toDisplayString());
//         }

//         if (destination.data->readOnly) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_WRITE_ACCESS_DENIED,
//                 url.toDisplayString());
//         }

//         if (!(flags & KIO::Overwrite)) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_FILE_ALREADY_EXIST,
//                 url.toDisplayString());
//         }
//     } else if (destination.error.code != RcloneErrorCode::NotFound) {
//         return rcloneFailure(destination.error, url, KIO::ERR_CANNOT_STAT);
//     }

//     bool sizeOk = false;
//     const qint64 sourceSize = metaData(
//         QStringLiteral("sourceSize")).toLongLong(&sizeOk);

//     if (sizeOk && sourceSize >= 0) {
//         totalSize(sourceSize);
//     }

//     const QString suffix = QFileInfo(
//         location->remotePath()).completeSuffix();
//     const QString extension = suffix.isEmpty()
//         ? QString()
//         : QLatin1Char('.') + suffix;
//     QTemporaryFile localUpload(
//         QDir::tempPath()
//         + QStringLiteral("/kio-rclone-upload-XXXXXX")
//         + extension);

//     if (!localUpload.open()) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_OPEN_FOR_WRITING,
//             localUpload.errorString());
//     }

//     qint64 stagedSize = 0;
//     infoMessage(i18n("Preparing upload…"));

//     while (true) {
//         if (wasKilled()) {
//             return KIO::WorkerResult::pass();
//         }

//         dataReq();
//         QByteArray sourceData;
//         const int readResult = readData(sourceData);

//         if (readResult < 0) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_CANNOT_READ,
//                 url.toDisplayString());
//         }

//         if (readResult == 0) {
//             break;
//         }

//         if (sizeOk && sourceSize >= 0
//             && stagedSize > sourceSize - sourceData.size()) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_CANNOT_WRITE,
//                 i18n("The upload source changed size while it was being read."));
//         }

//         if (localUpload.write(sourceData) != sourceData.size()) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_CANNOT_WRITE,
//                 localUpload.errorString());
//         }

//         stagedSize += sourceData.size();
//     }

//     if (sizeOk && sourceSize >= 0 && stagedSize != sourceSize) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_WRITE,
//             i18n("The upload source changed size while it was being read."));
//     }

//     if (!localUpload.flush()) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_WRITE,
//             localUpload.errorString());
//     }

//     const QString localUploadPath = localUpload.fileName();
//     localUpload.close();

//     if (!sizeOk || sourceSize < 0) {
//         totalSize(stagedSize);
//     }

//     RcloneWriteOptions options;
//     options.replaceExisting = flags & KIO::Overwrite;
//     options.expectedTarget = targetSnapshot;

//     infoMessage(i18n("Uploading…"));
//     const RcloneStatus status = m_client.upload(
//         localUploadPath,
//         location->toCliSpec(),
//         [this](const RcloneClient::TransferStats &progress) {
//             if (wasKilled()) {
//                 return;
//             }

//             processedSize(progress.bytes);

//             if (progress.speed >= 0) {
//                 const quint64 maximumSpeed =
//                     static_cast<quint64>(
//                         std::numeric_limits<unsigned long>::max());
//                 const quint64 boundedSpeed = qMin(
//                     static_cast<quint64>(progress.speed),
//                     maximumSpeed);
//                 speed(static_cast<unsigned long>(boundedSpeed));
//             }
//         },
//         options,
//         ctx);

//     if (!status.success()) {
//         if (status.error.code == RcloneErrorCode::AlreadyExists) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_FILE_ALREADY_EXIST,
//                 url.toDisplayString());
//         }

//         return rcloneFailure(status.error, url, KIO::ERR_CANNOT_WRITE);
//     }

//     return KIO::WorkerResult::pass();
// }

KIO::WorkerResult RcloneWorker::mkdir(const QUrl &url, int permissions)
{
    Q_UNUSED(permissions)

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
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

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_client.mkdir(
        location->toCliSpec(),
        ctx);

    if (!status.success()) {
        return RcloneWorkerHelpers::rcloneFailure(
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
        return RcloneWorkerHelpers::malformedUrlResult(source ? dest : src);
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

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const auto existingDestination = m_client.stat(
        destination->toCliSpec(),
        RcloneWorkerHelpers::statOptionsFor(*destination),
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
        return RcloneWorkerHelpers::rcloneFailure(
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

        return RcloneWorkerHelpers::rcloneFailure(status.error, src, KIO::ERR_CANNOT_RENAME);
    }

    return KIO::WorkerResult::pass();
}

// KIO::WorkerResult RcloneWorker::copy(const QUrl &src,
//                                      const QUrl &dest,
//                                      int permissions,
//                                      KIO::JobFlags flags)
// {
//     Q_UNUSED(permissions)

//     const auto source = RcloneLocation::parse(src);
//     const auto destination = RcloneLocation::parse(dest);

//     if (!source || !destination) {
//         return malformedUrlResult(source ? dest : src);
//     }

//     if (source->isRoot()
//         || source->isConfigureEntry()
//         || source->remotePath().isEmpty()
//         || destination->isRoot()
//         || destination->isConfigureEntry()
//         || destination->remotePath().isEmpty()) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_CANNOT_WRITE,
//             dest.toDisplayString());
//     }

//     const auto isDirectCopyLocation = [](const RcloneLocation &location) {
//         return location.kind() == RcloneLocation::Kind::Standard
//             || location.kind() == RcloneLocation::Kind::DriveMyDrive;
//     };

//     if (!isDirectCopyLocation(*source)
//         || !isDirectCopyLocation(*destination)) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_UNSUPPORTED_ACTION,
//             i18n("Copying items in this Google Drive view is not available yet."));
//     }

//     if (source->toCliSpec() == destination->toCliSpec()) {
//         return KIO::WorkerResult::fail(
//             KIO::ERR_IDENTICAL_FILES,
//             src.toDisplayString());
//     }

//     WorkerRcloneContext ctx(*this);
//     const auto existingDestination = m_client.stat(
//         destination->toCliSpec(),
//         statOptionsFor(*destination),
//         ctx);

//     if (existingDestination.success()) {
//         if (existingDestination.data->isDirectory) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_DIR_ALREADY_EXIST,
//                 dest.toDisplayString());
//         }

//         if (!(flags & KIO::Overwrite)) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_FILE_ALREADY_EXIST,
//                 dest.toDisplayString());
//         }
//     } else if (existingDestination.error.code != RcloneErrorCode::NotFound) {
//         return rcloneFailure(
//             existingDestination.error,
//             dest,
//             KIO::ERR_CANNOT_STAT);
//     }

//     RcloneWriteOptions options;
//     options.replaceExisting = flags & KIO::Overwrite;

//     const RcloneStatus status = m_client.copy(
//         source->toCliSpec(),
//         destination->toCliSpec(),
//         options,
//         ctx);

//     if (!status.success()) {
//         if (status.error.code == RcloneErrorCode::AlreadyExists) {
//             return KIO::WorkerResult::fail(
//                 KIO::ERR_FILE_ALREADY_EXIST,
//                 dest.toDisplayString());
//         }

//         return rcloneFailure(status.error, dest, KIO::ERR_CANNOT_WRITE);
//     }

//     return KIO::WorkerResult::pass();
// }

KIO::WorkerResult RcloneWorker::del(const QUrl &url,
                                    bool isFile)
{
    const int fallbackError = isFile
        ? KIO::ERR_CANNOT_DELETE
        : KIO::ERR_CANNOT_RMDIR;

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
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

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_client.remove(
        location->toCliSpec(),
        mode,
        ctx);

    if (!status.success()) {
        return RcloneWorkerHelpers::rcloneFailure(status.error, url, fallbackError);
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

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const auto space = m_client.spaceInfo(
        location->remoteName() + QLatin1Char(':'),
        ctx);

    if (!space.success()) {
        return RcloneWorkerHelpers::rcloneFailure(
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
RcloneWorker::open(const QUrl &,
                   QIODevice::OpenMode)
{
    return RcloneWorkerHelpers::randomAccessNotImplemented();
}

KIO::WorkerResult
RcloneWorker::read(KIO::filesize_t)
{
    return RcloneWorkerHelpers::randomAccessNotImplemented();
}

KIO::WorkerResult
RcloneWorker::write(const QByteArray &)
{
    return RcloneWorkerHelpers::randomAccessNotImplemented();
}

KIO::WorkerResult
RcloneWorker::seek(KIO::filesize_t)
{
    return RcloneWorkerHelpers::randomAccessNotImplemented();
}

KIO::WorkerResult
RcloneWorker::truncate(KIO::filesize_t)
{
    return RcloneWorkerHelpers::randomAccessNotImplemented();
}

KIO::WorkerResult
RcloneWorker::close()
{
    return RcloneWorkerHelpers::randomAccessNotImplemented();
}

KIO::WorkerResult
RcloneWorker::setModificationTime(const QUrl &url,
                                  const QDateTime &mtime)
{
    const auto location = RcloneLocation::parse(url);

    if (!location) {
        return RcloneWorkerHelpers::malformedUrlResult(url);
    }

    if (!mtime.isValid()
        || location->isRoot()
        || location->isConfigureEntry()
        || location->remotePath().isEmpty()
        || RcloneWorkerHelpers::isSyntheticDirectory(*location)) {
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

    RcloneWorkerHelpers::WorkerRcloneContext ctx(*this);
    const RcloneStatus status = m_client.setModificationTime(
        location->toCliSpec(),
        mtime,
        ctx);

    if (!status.success()) {
        return RcloneWorkerHelpers::rcloneFailure(
            status.error,
            url,
            KIO::ERR_CANNOT_SETTIME);
    }

    return KIO::WorkerResult::pass();
}
