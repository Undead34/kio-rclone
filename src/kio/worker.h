#pragma once

#include "rclone/client.h"
#include "rclone/navigation.h"
#include "rclone/remotefile.h"

#include <KIO/WorkerBase>
#include <QDateTime>
#include <QHash>
#include <QTemporaryFile>
#include <QUrl>

#include <memory>

class RcloneWorker : public KIO::WorkerBase
{
public:
    RcloneWorker(const QByteArray &protocol,
                 const QByteArray &poolSocket,
                 const QByteArray &appSocket);

    KIO::WorkerResult listDir(const QUrl &url) override;
    KIO::WorkerResult stat(const QUrl &url) override;
    KIO::WorkerResult mimetype(const QUrl &url) override;
    KIO::WorkerResult get(const QUrl &url) override;
    KIO::WorkerResult put(const QUrl &url, int permissions, KIO::JobFlags flags) override;
    KIO::WorkerResult mkdir(const QUrl &url, int permissions) override;
    KIO::WorkerResult rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags) override;
    KIO::WorkerResult copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags) override;
    KIO::WorkerResult del(const QUrl &url, bool isFile) override;
    KIO::WorkerResult fileSystemFreeSpace(const QUrl &url) override;

    // Random operations

    KIO::WorkerResult open(const QUrl &url,
                           QIODevice::OpenMode mode) override;

    KIO::WorkerResult read(KIO::filesize_t size) override;

    KIO::WorkerResult write(const QByteArray &data) override;

    KIO::WorkerResult seek(KIO::filesize_t offset) override;

    KIO::WorkerResult truncate(KIO::filesize_t length) override;

    KIO::WorkerResult close() override;

    KIO::WorkerResult setModificationTime(const QUrl &url,
                                          const QDateTime &mtime) override;

private:
    [[nodiscard]] RcloneStatus
    materializeUnknownSizeFile(const QString &remoteSpec,
                               RcloneItem &item);

    [[nodiscard]] bool
    cachedMaterializationMatches(const QString &remoteSpec,
                                 const RcloneItem &item) const;

    [[nodiscard]] KIO::WorkerResult
    sendCachedMaterialization(const QUrl &url);

    void clearMaterializationCache();

    RcloneClient m_client;
    RcloneNavigation m_navigation;

    std::unique_ptr<RcloneRemoteFile> m_openFile;
    QUrl m_openUrl;

    // One fully materialized virtual export is enough to bridge the common
    // stat() -> get() sequence without downloading it twice.
    std::unique_ptr<QTemporaryFile> m_materializedFile;
    QString m_materializedRemoteSpec;
    RcloneTargetSnapshot m_materializedSource;
};

// /*
//  * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
//  *
//  * SPDX-License-Identifier: GPL-2.0-or-later
//  */

// #pragma once

// #include "cache/directorysnapshotcache.h"
// #include "rclone/location.h"
// #include "rclone/client.h"

// #include <KIO/WorkerBase>
// #include <QHash>
// #include <QTemporaryFile>

// #include <memory>
// #include <optional>

// class RcloneUrl;
// struct DirectoryListingPolicy;
// class RcloneRecursiveListing;

// class RcloneWorker : public KIO::WorkerBase
// {
//   public:
//       RcloneWorker(const QByteArray &protocol,
//                    const QByteArray &poolSocket,
//                    const QByteArray &appSocket);

//       // List the contents of a directory URL. Emit entries with listEntry() or
//       // listEntries(); fail with ERR_CANNOT_ENTER_DIRECTORY when the directory cannot
//       // be accessed, and redirect instead of listing an empty path.
//       KIO::WorkerResult listDir(const QUrl &url) override;

//       // Return information for exactly one file or directory. Build one UDSEntry and
//       // report it with statEntry(); honor the "details" metadata to avoid expensive
//       // remote lookups when KIO only needs basic probing.
//       KIO::WorkerResult stat(const QUrl &url) override;

//       // Determine the MIME type for one URL. Prefer emitting mimeType() directly;
//       // otherwise provide enough data for detection without forcing a full download.
//       KIO::WorkerResult mimetype(const QUrl &url) override;

//       // Read a file. Emit mimeType() before sending file contents with data(); send
//       // progress for long transfers and stop promptly if the worker was killed.
//       KIO::WorkerResult get(const QUrl &url) override;

//       // Write a file from job-provided data. Honor Overwrite, treat permissions == -1
//       // as "do not set special permissions", and use the "modified" metadata to
//       // preserve the destination modification time when available.
//       KIO::WorkerResult put(const QUrl &url, int permissions, KIO::JobFlags flags) override;

//       // Create a directory. Apply permissions only when permissions != -1, and return
//       // the appropriate mkdir failure, such as ERR_CANNOT_MKDIR, when creation fails.
//       KIO::WorkerResult mkdir(const QUrl &url, int permissions) override;

//       // Move or rename an entry. Honor Overwrite and check destination existence here:
//       // KIO does not stat the destination beforehand; return ERR_UNSUPPORTED_ACTION
//       // if KIO should fall back to copy + del.
//       KIO::WorkerResult rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags) override;

//       // Copy one file to another URL. Honor Overwrite, preserve the source
//       // modification time on the destination, and return ERR_UNSUPPORTED_ACTION when
//       // KIO should fall back to get + put.
//       KIO::WorkerResult copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags) override;

//       // Delete a file or directory. The isFile argument tells what KIO expects;
//       // non-empty directory deletion should fail unless recursive deletion is enabled
//       // by the protocol file and metadata("recurse") == "true".
//       KIO::WorkerResult del(const QUrl &url, bool isFile) override;

//       // Report total and available space for the filesystem/account behind the URL,
//       // typically by setting "total" and "available" metadata before returning.
//       KIO::WorkerResult fileSystemFreeSpace(const QUrl &url) override;

//   private:
//       [[nodiscard]] KIO::WorkerResult listRoot(const QUrl &requestUrl);
//       [[nodiscard]] KIO::WorkerResult listDriveHub(const RcloneLocation &location);

//       [[nodiscard]] KIO::WorkerResult listDriveTrash(const QUrl &requestUrl,
//                                                       const RcloneLocation &directory,
//                                                       const DirectoryListingPolicy &policy);

//       [[nodiscard]] KIO::WorkerResult listSharedDrives(const QUrl &requestUrl, const RcloneLocation &location);
//       [[nodiscard]] KIO::WorkerResult listFoldersIndex(const RcloneLocation &location);
//       [[nodiscard]] KIO::WorkerResult listRemoteDirectory(const QUrl &requestUrl,
//                                                            const RcloneLocation &directory,
//                                                            const DirectoryListingPolicy &policy);
//       [[nodiscard]] KIO::WorkerResult listUniqueDirectory(const QUrl &requestUrl,
//                                                            const RcloneLocation &directory,
//                                                            const DirectoryListingPolicy &policy);
//       [[nodiscard]] KIO::WorkerResult listDuplicateSafeDirectory(const QUrl &requestUrl,
//                                                                   const RcloneLocation &directory,
//                                                                   const DirectoryListingPolicy &policy);
//       [[nodiscard]] std::optional<QList<RcloneItem>> cachedDirectory(const RcloneLocation &directory,
//                                                                        const DirectoryListingPolicy &policy);
//       void publishDirectoryEntries(const RcloneLocation &directory, const QList<RcloneItem> &items);

//       [[nodiscard]] KIO::WorkerResult ensureRcloneClient() const;
//       [[nodiscard]] std::optional<RcloneLocation> resolveLocation(const RcloneUrl &url, QString *error = nullptr);
//       [[nodiscard]] std::optional<bool> isGoogleDriveRemote(const QString &remote, QString *error = nullptr);
//       [[nodiscard]] KIO::UDSEntry currentDirectoryEntry(const RcloneLocation &location,
//                                                          const QString &entryName = QStringLiteral(".")) const;

//       [[nodiscard]] std::optional<bool> remoteMayHaveDuplicateNames(const RcloneLocation &location);
//       [[nodiscard]] KIO::WorkerResult ensureUnambiguousParentDirectories(const RcloneLocation &location,
//                                                                           int fallbackError) const;
//       [[nodiscard]] std::optional<RcloneItem> sourceItem(const RcloneLocation &location,
//                                                          QString *error = nullptr) const;
//       [[nodiscard]] std::optional<RcloneItem> cachedItemForReadOnlyRequest(const RcloneLocation &location);
//       [[nodiscard]] std::optional<RcloneItem> rememberedSyntheticTrashItem(const RcloneLocation &location) const;
//       void rememberSyntheticTrashItems(const RcloneLocation &root, const RcloneRecursiveListing &listing);
//       void invalidateDirectorySnapshots();

//       [[nodiscard]] KIO::WorkerResult cacheRemoteFile(const QUrl &url, const RcloneLocation &location, RcloneItem &item);
//       [[nodiscard]] KIO::WorkerResult resolveUnknownSize(const QUrl &url, const RcloneLocation &location, RcloneItem &item);
//       [[nodiscard]] bool cachedDownloadMatches(const RcloneLocation &location, const RcloneItem &item) const;
//       [[nodiscard]] KIO::WorkerResult sendCachedDownload(const QUrl &url);
//       void clearCachedDownload();

//       [[nodiscard]] RcloneResult runCommand(const QStringList &arguments,
//                                             int timeoutMs = 120000) const;

//       [[nodiscard]] RcloneResult runUpload(const QString &localPath,
//                                            const QString &remoteSpec);

//       [[nodiscard]] KIO::WorkerResult commandResult(const RcloneResult &result,
//                                                     int fallbackError,
//                                                     const QUrl &url) const;

//       [[nodiscard]] KIO::WorkerResult errorResult(const QString &message,
//                                                   int fallbackError,
//                                                   const QUrl &url) const;

//       RcloneClient m_rclone;
//       DirectorySnapshotCache m_directorySnapshots;

//       // Capability lookup is slow and remote-specific. A worker process can
//       // safely reuse it until its root listing is refreshed.
//       QHash<QString, bool> m_remoteDuplicateNameSupport;
//       QHash<QString, QString> m_remoteTypes;
//       QHash<QString, QString> m_sharedDriveNames;
//       QHash<QString, RcloneItem> m_syntheticTrashItems;

//       // Caché usada por get(); no debe reutilizarse como estado de FileJob sin controlar posición y modo de apertura.
//       std::unique_ptr<QTemporaryFile> m_cachedDownload;

//       // Identifica exactamente el objeto remoto asociado con la descarga almacenada en caché.
//       QString m_cachedDownloadSpec;

//       // Permite invalidar la caché cuando el archivo remoto cambia entre operaciones.
//       QString m_cachedDownloadVersion;
// };
