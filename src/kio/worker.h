/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "cache/directorysnapshotcache.h"
#include "rclone/rcloneclient.h"

#include <KIO/WorkerBase>
#include <QHash>
#include <QTemporaryFile>

#include <memory>
#include <optional>

class RcloneUrl;
struct DirectoryListingPolicy;

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

  private:
      [[nodiscard]] KIO::WorkerResult listRoot(const QUrl &requestUrl);
      [[nodiscard]] KIO::WorkerResult listRemoteDirectory(const QUrl &requestUrl,
                                                           const RcloneUrl &directory,
                                                           const DirectoryListingPolicy &policy);
      [[nodiscard]] KIO::WorkerResult listUniqueDirectory(const QUrl &requestUrl,
                                                           const RcloneUrl &directory,
                                                           const DirectoryListingPolicy &policy);
      [[nodiscard]] KIO::WorkerResult listDuplicateSafeDirectory(const QUrl &requestUrl,
                                                                  const RcloneUrl &directory,
                                                                  const DirectoryListingPolicy &policy);
      [[nodiscard]] std::optional<QList<RcloneItem>> cachedDirectory(const RcloneUrl &directory,
                                                                       const DirectoryListingPolicy &policy);
      void publishDirectoryEntries(const RcloneUrl &directory, const QList<RcloneItem> &items);

      [[nodiscard]] KIO::WorkerResult ensureRcloneClient() const;

      [[nodiscard]] std::optional<bool> remoteMayHaveDuplicateNames(const QString &remote);
      [[nodiscard]] KIO::WorkerResult ensureUnambiguousParentDirectories(const RcloneUrl &url,
                                                                          int fallbackError) const;
      [[nodiscard]] std::optional<RcloneItem> sourceItem(const RcloneUrl &url,
                                                         QString *error = nullptr) const;
      [[nodiscard]] std::optional<RcloneItem> sourceItem(const QString &remote,
                                                         const QString &remotePath,
                                                         QString *error = nullptr) const;
      [[nodiscard]] std::optional<RcloneItem> cachedItemForReadOnlyRequest(const RcloneUrl &url);
      void invalidateDirectorySnapshots();

      [[nodiscard]] KIO::WorkerResult cacheRemoteFile(const RcloneUrl &url, RcloneItem &item);
      [[nodiscard]] KIO::WorkerResult resolveUnknownSize(const RcloneUrl &url, RcloneItem &item);
      [[nodiscard]] bool cachedDownloadMatches(const RcloneUrl &url, const RcloneItem &item) const;
      [[nodiscard]] KIO::WorkerResult sendCachedDownload(const RcloneUrl &url);
      void clearCachedDownload();

      [[nodiscard]] RcloneResult runCommand(const QStringList &arguments,
                                            int timeoutMs = 120000) const;
      [[nodiscard]] RcloneResult runUpload(const QString &localPath,
                                           const QString &remoteSpec);

      [[nodiscard]] KIO::WorkerResult commandResult(const RcloneResult &result,
                                                    int fallbackError,
                                                    const QUrl &url) const;
      [[nodiscard]] KIO::WorkerResult errorResult(const QString &message,
                                                  int fallbackError,
                                                  const QUrl &url) const;

      RcloneClient m_rclone;
      DirectorySnapshotCache m_directorySnapshots;

      // Capability lookup is slow and remote-specific. A worker process can
      // safely reuse it until its root listing is refreshed.
      QHash<QString, bool> m_remoteDuplicateNameSupport;

      // Caché usada por get(); no debe reutilizarse como estado de FileJob sin controlar posición y modo de apertura.
      std::unique_ptr<QTemporaryFile> m_cachedDownload;

      // Identifica exactamente el objeto remoto asociado con la descarga almacenada en caché.
      QString m_cachedDownloadSpec;

      // Permite invalidar la caché cuando el archivo remoto cambia entre operaciones.
      QString m_cachedDownloadVersion;
};
