/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclonebackend.h"

#include <KIO/WorkerBase>
#include <QTemporaryFile>

#include <memory>

class QProcess;
class RcloneUrl;

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
      // Confirmado con rclone v1.74.4: local expone "mode" (octal, escribible) en `lsjson --metadata`;
      // backends cloud (drive/s3/dropbox/etc.) no tienen ese concepto, ahí debe fallar directo.
      // Plan: (1) agregar --metadata a list()/stat() en RcloneBackend y parsear "mode" en RcloneItem;
      // (2) si el item no trae "mode" -> fail(ERR_UNSUPPORTED_ACTION) sin intentar nada más;
      // (3) si lo trae -> moveto remoteSpec a un nombre temporal y moveto de vuelta con
      //     --metadata --metadata-set mode=<octal, preservando bits de tipo S_IFREG/S_IFDIR>.
      //     copyto/moveto sobre el mismo path es no-op en rclone (probado), por eso el salto por nombre
      //     temporal; moveto es barato porque casi todos los backends lo resuelven server-side.
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
      [[nodiscard]] KIO::UDSEntry rootEntry() const;
      [[nodiscard]] KIO::UDSEntry configureEntry() const;
      [[nodiscard]] KIO::UDSEntry remoteEntry(const QString &name, bool currentDirectory = false, const QString &type = {}) const;
      [[nodiscard]] KIO::UDSEntry itemEntry(const RcloneItem &item) const;

      [[nodiscard]] KIO::WorkerResult ensureBackend() const;

      [[nodiscard]] bool remoteExists(const QString &remote, QString *error = nullptr) const;
      [[nodiscard]] bool destinationExists(const QString &remoteSpec,
                                           std::optional<RcloneItem> *item,
                                           QString *error = nullptr) const;
      [[nodiscard]] std::optional<RcloneItem> sourceItem(const RcloneUrl &url,
                                                         QString *error = nullptr) const;

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

      static void configureProcess(QProcess &process,
                                   const QString &program,
                                   const QStringList &arguments);
      static void stopProcess(QProcess &process);
      static void collectProcessOutput(QProcess &process,
                                       QByteArray &standardOutput,
                                       QByteArray &standardError);

      RcloneBackend m_backend;

      // Caché usada por get(); no debe reutilizarse como estado de FileJob sin controlar posición y modo de apertura.
      std::unique_ptr<QTemporaryFile> m_cachedDownload;

      // Identifica exactamente el objeto remoto asociado con la descarga almacenada en caché.
      QString m_cachedDownloadSpec;

      // Permite invalidar la caché cuando el archivo remoto cambia entre operaciones.
      QString m_cachedDownloadVersion;
};
