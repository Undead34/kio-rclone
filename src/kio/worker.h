/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "cache/directorysnapshotcache.h"
#include "rclone/location.h"
#include "rclone/client.h"

#include <KIO/WorkerBase>
#include <QHash>
#include <QTemporaryFile>

#include <memory>
#include <optional>

class RcloneUrl;
struct DirectoryListingPolicy;
class RcloneRecursiveListing;

class RcloneWorker : public KIO::WorkerBase
{
  public:
      RcloneWorker(const QByteArray &protocol,
                   const QByteArray &poolSocket,
                   const QByteArray &appSocket);

      // List the contents of a directory URL. Emit entries with listEntry() or
      // listEntries(); fail with ERR_CANNOT_ENTER_DIRECTORY when the directory cannot
      // be accessed, and redirect instead of listing an empty path.
      KIO::WorkerResult listDir(const QUrl &url) override;

      // Return information for exactly one file or directory. Build one UDSEntry and
      // report it with statEntry(); honor the "details" metadata to avoid expensive
      // remote lookups when KIO only needs basic probing.
      KIO::WorkerResult stat(const QUrl &url) override;

      // Determine the MIME type for one URL. Prefer emitting mimeType() directly;
      // otherwise provide enough data for detection without forcing a full download.
      KIO::WorkerResult mimetype(const QUrl &url) override;

      // Read a file. Emit mimeType() before sending file contents with data(); send
      // progress for long transfers and stop promptly if the worker was killed.
      KIO::WorkerResult get(const QUrl &url) override;

      // Write a file from job-provided data. Honor Overwrite, treat permissions == -1
      // as "do not set special permissions", and use the "modified" metadata to
      // preserve the destination modification time when available.
      KIO::WorkerResult put(const QUrl &url, int permissions, KIO::JobFlags flags) override;

      // Create a directory. Apply permissions only when permissions != -1, and return
      // the appropriate mkdir failure, such as ERR_CANNOT_MKDIR, when creation fails.
      KIO::WorkerResult mkdir(const QUrl &url, int permissions) override;

      // Move or rename an entry. Honor Overwrite and check destination existence here:
      // KIO does not stat the destination beforehand; return ERR_UNSUPPORTED_ACTION
      // if KIO should fall back to copy + del.
      KIO::WorkerResult rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags) override;

      // Copy one file to another URL. Honor Overwrite, preserve the source
      // modification time on the destination, and return ERR_UNSUPPORTED_ACTION when
      // KIO should fall back to get + put.
      KIO::WorkerResult copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags) override;

      // Delete a file or directory. The isFile argument tells what KIO expects;
      // non-empty directory deletion should fail unless recursive deletion is enabled
      // by the protocol file and metadata("recurse") == "true".
      KIO::WorkerResult del(const QUrl &url, bool isFile) override;

      // Report total and available space for the filesystem/account behind the URL,
      // typically by setting "total" and "available" metadata before returning.
      KIO::WorkerResult fileSystemFreeSpace(const QUrl &url) override;

      // TODO: Revisar callbacks de rclone para mostrar autenticación interactiva y mensajes de progreso en KIO.
      // log_callback? auth_callback?

      // TODO: Implementar solo si se mantiene una sesión o proceso rclone persistente entre operaciones.
      // KIO::WorkerResult openConnection() override;
      // KIO::WorkerResult closeConnection() override;

      // TODO: Revisar si la URL necesita separar explícitamente host, remote y ruta en el estado del worker.
      // KIO::WorkerResult setHost(const QString &host,
      //                           quint16 port,
      //                           const QString &user,
      //                           const QString &password) override;

      // TODO: Simular chmod vía la metadata genérica de rclone (mode/uid/gid); no todos los backends la exponen.
      // KIO::WorkerResult chmod(const QUrl &url, int permissions) override;

      // TODO: Implementar para conservar la fecha de modificación al subir o actualizar archivos.
      // put() debe leer metaData("modified") (ISO date) y aplicarlo al QTemporaryFile local con
      // QFile::setFileTime() ANTES de runUpload(), ya que copyto preserva el mtime de origen por defecto.
      // Patrón de referencia: kio-extras/sftp/kio_sftp.cpp:1639-1659 (mismo enfoque al final de su put()).
      // Además implementar el override standalone con `rclone touch -t <ISO> remoteSpec` para cuando
      // KIO lo invoque fuera de un put() (p.ej. CopyJob fijando el mtime de directorios).
      // KIO::WorkerResult setModificationTime(const QUrl &url,
      //                                       const QDateTime &mtime) override;

      // TODO: Implementar symlink únicamente para remotos backend "drive" (Google Drive), vía
      // `rclone backend shortcut`. Confirmado con `rclone backend help <tipo>`: solo "drive" tiene ese
      // comando entre los backends probados (onedrive/dropbox/box/s3/mega/pcloud/gcs no lo tienen).
      // Plan: comprobar RcloneBackend::remoteInfo(dest.remote())->type == "drive" antes de intentar;
      // si no -> fail(ERR_UNSUPPORTED_ACTION). "target" puede llegar como URL rclone: o como ruta
      // relativa; resolverlo contra el mismo remote que "dest" (shortcuts cross-remote no aplican
      // salvo pasando -o target=otroRemote:, que no vamos a soportar por ahora).
      // KIO::WorkerResult symlink(const QString &target,
      //                           const QUrl &dest,
      //                           KIO::JobFlags flags) override;

      /// KIO::FileJob interface

      // TODO: Implementar FileJob para streaming con seek, reproducción de videos y acceso aleatorio.
      // KIO::WorkerResult open(const QUrl &url, QIODevice::OpenMode mode) override;

      // TODO: En ReadOnly, leer rangos remotos sin descargar previamente el archivo completo.
      // KIO::WorkerResult read(KIO::filesize_t size) override;

      // TODO: En escritura, usar staging local porque los remotos no suelen aceptar modificaciones aleatorias.
      // KIO::WorkerResult write(const QByteArray &data) override;

      // TODO: En lectura remota, cambiar la posición lógica usada por la siguiente petición por rango.
      // KIO::WorkerResult seek(KIO::filesize_t offset) override;

      // TODO: En escritura, redimensionar el archivo temporal y subir la nueva versión al cerrar.
      // KIO::WorkerResult truncate(KIO::filesize_t length) override;

      // TODO: Al cerrar, cancelar la lectura o subir el archivo temporal únicamente si fue modificado.
      // KIO::WorkerResult close() override;

      // TODO: Reservar special para comandos propios que no encajen en las operaciones estándar de KIO.
      // KIO::WorkerResult special(const QByteArray &data) override;

  private:
      [[nodiscard]] KIO::WorkerResult listRoot(const QUrl &requestUrl);
      [[nodiscard]] KIO::WorkerResult listDriveHub(const RcloneLocation &location);

      [[nodiscard]] KIO::WorkerResult listDriveTrash(const QUrl &requestUrl,
                                                      const RcloneLocation &directory,
                                                      const DirectoryListingPolicy &policy);

      [[nodiscard]] KIO::WorkerResult listSharedDrives(const QUrl &requestUrl, const RcloneLocation &location);
      [[nodiscard]] KIO::WorkerResult listFoldersIndex(const RcloneLocation &location);
      [[nodiscard]] KIO::WorkerResult listRemoteDirectory(const QUrl &requestUrl,
                                                           const RcloneLocation &directory,
                                                           const DirectoryListingPolicy &policy);
      [[nodiscard]] KIO::WorkerResult listUniqueDirectory(const QUrl &requestUrl,
                                                           const RcloneLocation &directory,
                                                           const DirectoryListingPolicy &policy);
      [[nodiscard]] KIO::WorkerResult listDuplicateSafeDirectory(const QUrl &requestUrl,
                                                                  const RcloneLocation &directory,
                                                                  const DirectoryListingPolicy &policy);
      [[nodiscard]] std::optional<QList<RcloneItem>> cachedDirectory(const RcloneLocation &directory,
                                                                       const DirectoryListingPolicy &policy);
      void publishDirectoryEntries(const RcloneLocation &directory, const QList<RcloneItem> &items);

      [[nodiscard]] KIO::WorkerResult ensureRcloneClient() const;
      [[nodiscard]] std::optional<RcloneLocation> resolveLocation(const RcloneUrl &url, QString *error = nullptr);
      [[nodiscard]] std::optional<bool> isGoogleDriveRemote(const QString &remote, QString *error = nullptr);
      [[nodiscard]] KIO::UDSEntry currentDirectoryEntry(const RcloneLocation &location,
                                                         const QString &entryName = QStringLiteral(".")) const;

      [[nodiscard]] std::optional<bool> remoteMayHaveDuplicateNames(const RcloneLocation &location);
      [[nodiscard]] KIO::WorkerResult ensureUnambiguousParentDirectories(const RcloneLocation &location,
                                                                          int fallbackError) const;
      [[nodiscard]] std::optional<RcloneItem> sourceItem(const RcloneLocation &location,
                                                         QString *error = nullptr) const;
      [[nodiscard]] std::optional<RcloneItem> cachedItemForReadOnlyRequest(const RcloneLocation &location);
      [[nodiscard]] std::optional<RcloneItem> rememberedSyntheticTrashItem(const RcloneLocation &location) const;
      void rememberSyntheticTrashItems(const RcloneLocation &root, const RcloneRecursiveListing &listing);
      void invalidateDirectorySnapshots();

      [[nodiscard]] KIO::WorkerResult cacheRemoteFile(const QUrl &url, const RcloneLocation &location, RcloneItem &item);
      [[nodiscard]] KIO::WorkerResult resolveUnknownSize(const QUrl &url, const RcloneLocation &location, RcloneItem &item);
      [[nodiscard]] bool cachedDownloadMatches(const RcloneLocation &location, const RcloneItem &item) const;
      [[nodiscard]] KIO::WorkerResult sendCachedDownload(const QUrl &url);
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
      QHash<QString, QString> m_remoteTypes;
      QHash<QString, QString> m_sharedDriveNames;
      QHash<QString, RcloneItem> m_syntheticTrashItems;

      // Caché usada por get(); no debe reutilizarse como estado de FileJob sin controlar posición y modo de apertura.
      std::unique_ptr<QTemporaryFile> m_cachedDownload;

      // Identifica exactamente el objeto remoto asociado con la descarga almacenada en caché.
      QString m_cachedDownloadSpec;

      // Permite invalidar la caché cuando el archivo remoto cambia entre operaciones.
      QString m_cachedDownloadVersion;
};
