/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "client.h"
#include "syncrepository.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>

/**
 * @brief Estados del ciclo de vida del archivo (Estilo OneDrive / On-Demand).
 */
enum class RcloneFileSyncState {
    CloudOnly,      ///< No hay copia local. Solo existe en el remoto.
    SyncingDown,    ///< Descargando el archivo completo a la caché local.
    Synced,         ///< La caché local está actualizada con el remoto.
    Modified,       ///< Modificado localmente, pendiente de subida (Dirty).
    SyncingUp,      ///< Subiendo los cambios locales al remoto.
    Conflict,       ///< Error de sincronización o colisión detectada.
};

/**
 * @brief Configuración del gestor de caché local.
 */
struct RcloneCacheConfig {
    /// Ruta base donde residirán los archivos físicamente (ej. ~/.cache/kio-rclone)
    QString baseCacheDirectory;
};

/**
 * @brief Archivo remoto con caché local completa y gestión de estado.
 *
 * Abandona la lectura dispersa (sparse) en favor de una descarga atómica inicial.
 * Provee la ruta física real (localCachePath) para permitir redirecciones
 * transparentes en KIO y lecturas a velocidad nativa del disco.
 */
class RcloneRemoteFile
{
public:
    explicit RcloneRemoteFile(
        RcloneClient &client,
        RcloneSyncRepository &repository,
        RcloneCacheConfig config);

    /**
     * @brief Prepara el archivo para I/O.
     *
     * Si el archivo no está en caché local, se bloqueará aquí realizando la
     * descarga completa (SyncingDown -> Synced). Si se abre en modo escritura
     * con truncamiento, asume estado Modified inmediatamente.
     */
    [[nodiscard]] RcloneStatus
    open(const QString &remoteSpec,
         QIODevice::OpenMode mode,
         const RcloneContext &ctx);

    [[nodiscard]] RcloneResponse<QByteArray>
    read(qint64 size);

    [[nodiscard]] RcloneStatus
    write(const QByteArray &data);

    [[nodiscard]] RcloneStatus
    seek(qint64 offset);

    [[nodiscard]] RcloneStatus
    truncate(qint64 size);

    [[nodiscard]] RcloneStatus
    flush();

    /**
     * @brief Cierra el archivo y sincroniza si es necesario.
     *
     * Si el estado es Modified, cambia a SyncingUp y realiza la subida completa.
     * Retorna a estado Synced en caso de éxito.
     */
    [[nodiscard]] RcloneStatus
    close(const RcloneContext &ctx);

    /**
     * @brief Fuerza la eliminación de la caché local de este archivo.
     */
    void evictLocalCache();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] qint64 position() const;
    [[nodiscard]] qint64 size() const;

    /**
     * @brief Estado actual de sincronización del archivo.
     */
    [[nodiscard]] RcloneFileSyncState state() const;

    /**
     * @brief Ruta absoluta del archivo en el sistema de archivos local.
     * Útil para que el KIO Worker pueda emitir KIO::redirection(QUrl::fromLocalFile(...)).
     */
    [[nodiscard]] QString localCachePath() const;

private:
    [[nodiscard]] RcloneStatus
    ensureFullyDownloaded(const RcloneContext &ctx);

    [[nodiscard]] RcloneStatus
    performUpload(const RcloneContext &ctx);

    void reset();
    void updateState(RcloneFileSyncState newState);
    QString resolveLocalPath(const QString &remoteSpec) const;

    RcloneClient &m_client;
    RcloneSyncRepository &m_repository;
    RcloneCacheConfig m_config;

    QString m_remoteSpec;
    QString m_localPath;

    QFile m_localFile;
    QIODevice::OpenMode m_mode = QIODevice::NotOpen;

    RcloneFileSyncState m_state = RcloneFileSyncState::CloudOnly;
    RcloneTargetSnapshot m_originalTarget;
};
