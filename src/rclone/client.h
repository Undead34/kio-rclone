/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "models.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

/**
 * @brief Categoría de error independiente del texto producido por rclone.
 */
enum class RcloneErrorCode {
    None,
    Unknown,
    Cancelled,
    Aborted,
    TimedOut,
    NotFound,
    PermissionDenied,
    AlreadyExists,
    Unsupported,
    InvalidResponse,
    ProcessFailure,
};

/**
 * @brief Error producido por una operación del cliente.
 *
 * Las capas superiores deben utilizar code para tomar decisiones y tratar
 * message exclusivamente como información de diagnóstico.
 */
struct RcloneError {
    RcloneErrorCode code = RcloneErrorCode::Unknown;
    QString message;
    int exitCode = -1;
};

/**
 * @brief Resultado de una operación que produce un valor.
 */
template <typename T>
struct RcloneResponse {
    std::optional<T> data;
    RcloneError error;

    [[nodiscard]] bool success() const
    {
        return data.has_value();
    }
};

/**
 * @brief Resultado de una operación que no produce un valor.
 */
struct RcloneStatus {
    bool ok = false;
    RcloneError error;

    [[nodiscard]] bool success() const
    {
        return ok;
    }
};

/**
 * @brief Contexto de cancelación proporcionado por el consumidor.
 */
class RcloneContext
{
public:
    virtual ~RcloneContext() = default;

    [[nodiscard]] virtual bool isCancelled() const = 0;
};

/**
 * @brief Política utilizada al eliminar un elemento remoto.
 */
enum class RcloneRemovalMode {
    File,
    EmptyDirectory,
    Recursive,
};

/**
 * @brief Versión conocida de un destino remoto.
 *
 * Permite comprobar si el elemento remoto cambió mientras una copia local
 * estaba siendo preparada o modificada.
 */
struct RcloneTargetSnapshot {
    bool exists = false;
    bool isDirectory = false;

    QString id;

    QDateTime modificationTime;

    qint64 size = -1;

    [[nodiscard]] static RcloneTargetSnapshot
    fromItem(const RcloneItem &item)
    {
        return {
            true,
            item.isDirectory,
            item.id,
            item.modificationTime,
            item.size,
        };
    }
};

/**
 * @brief Política para operaciones que publican un destino remoto.
 */
struct RcloneWriteOptions {
    bool replaceExisting = false;

    /**
     * @brief Estado del destino que el consumidor espera encontrar.
     *
     * upload() puede utilizar este snapshot para detectar modificaciones
     * remotas concurrentes antes de publicar los datos locales.
     */
    std::optional<RcloneTargetSnapshot> expectedTarget;
};

/**
 * @brief Opciones adicionales para listados y consultas de metadata.
 */
struct RcloneListOptions {
    bool recursive = false;

    QStringList extraArguments;
};

/**
 * @brief Cliente bloqueante y agnóstico de KIO para el binario rclone.
 *
 * Esta clase representa exclusivamente operaciones contra el backend remoto:
 *
 * - ejecución de rclone;
 * - parsing de sus respuestas;
 * - traducción de errores;
 * - transferencias completas;
 * - operaciones de metadata.
 *
 * No administra caché local, archivos abiertos ni estados de sincronización.
 */
class RcloneClient
{
public:
    explicit RcloneClient(QString executable = {});

    /**
     * @brief Ruta del ejecutable utilizado por el cliente.
     */
    [[nodiscard]] QString executable() const;

    /**
     * @brief Comprueba que el ejecutable de rclone está disponible.
     *
     * No comprueba remotos ni conectividad.
     */
    [[nodiscard]] bool isAvailable() const;

    /**
     * @brief Obtiene los remotos configurados y sus tipos.
     */
    [[nodiscard]] RcloneResponse<QList<RcloneRemote>>
    listRemotes(const RcloneContext &ctx) const;

    /**
     * @brief Ejecuta un subcomando no interactivo de rclone config.
     */
    [[nodiscard]] RcloneResponse<QByteArray>
    runConfigCommand(const QStringList &arguments,
                     const RcloneContext &ctx) const;

    /**
     * @brief Obtiene las métricas de espacio disponibles para un remoto.
     */
    [[nodiscard]] RcloneResponse<RcloneSpace>
    spaceInfo(const QString &remoteSpec,
              const RcloneContext &ctx) const;

    /**
     * @brief Obtiene metadata de un archivo o directorio remoto.
     */
    [[nodiscard]] RcloneResponse<RcloneItem>
    stat(const QString &remoteSpec,
         const RcloneContext &ctx) const;

    /**
     * @brief Obtiene metadata respetando opciones específicas de la vista.
     */
    [[nodiscard]] RcloneResponse<RcloneItem>
    stat(const QString &remoteSpec,
         const RcloneListOptions &options,
         const RcloneContext &ctx) const;

    using ItemCallback =
        std::function<bool(const RcloneItem &)>;

    /**
     * @brief Lista un directorio incrementalmente.
     */
    [[nodiscard]] RcloneStatus
    list(const QString &remoteSpec,
         bool recursive,
         const ItemCallback &onItem,
         const RcloneContext &ctx) const;

    /**
     * @brief Lista un directorio con opciones específicas del backend.
     */
    [[nodiscard]] RcloneStatus
    list(const QString &remoteSpec,
         const RcloneListOptions &options,
         const ItemCallback &onItem,
         const RcloneContext &ctx) const;

    /**
     * @brief Crea un directorio remoto.
     */
    [[nodiscard]] RcloneStatus
    mkdir(const QString &remoteSpec,
          const RcloneContext &ctx) const;

    /**
     * @brief Cambia la fecha de modificación de un elemento remoto.
     */
    [[nodiscard]] RcloneStatus
    setModificationTime(const QString &remoteSpec,
                        const QDateTime &mtime,
                        const RcloneContext &ctx) const;

    /**
     * @brief Elimina un archivo o directorio remoto.
     */
    [[nodiscard]] RcloneStatus
    remove(const QString &remoteSpec,
           RcloneRemovalMode mode,
           const RcloneContext &ctx) const;

    /**
     * @brief Mueve o renombra un elemento remoto.
     */
    [[nodiscard]] RcloneStatus
    move(const QString &srcSpec,
         const QString &destSpec,
         const RcloneWriteOptions &options,
         const RcloneContext &ctx) const;

    /**
     * @brief Copia un elemento remoto.
     */
    [[nodiscard]] RcloneStatus
    copy(const QString &srcSpec,
         const QString &destSpec,
         const RcloneWriteOptions &options,
         const RcloneContext &ctx) const;

    using DownloadCallback =
        std::function<bool(const QByteArray &chunk)>;

    /**
     * @brief Descarga completamente un archivo remoto mediante streaming.
     *
     * La creación de la copia física local corresponde a RcloneRemoteFile.
     * El cliente simplemente entrega los bloques conforme rclone los produce.
     */
    [[nodiscard]] RcloneStatus
    download(const QString &remoteSpec,
             const DownloadCallback &onChunk,
             const RcloneContext &ctx) const;

    /**
     * @brief Estadísticas informadas durante una transferencia.
     */
    struct TransferStats {
        int percentage = -1;

        qint64 bytes = 0;

        qint64 speed = -1;

        qint64 etaSeconds = -1;
    };

    using UploadCallback =
        std::function<void(const TransferStats &)>;

    /**
     * @brief Publica un archivo local completo en el destino remoto.
     *
     * La implementación puede transferir primero hacia un destino temporal y
     * comprobar expectedTarget antes de sustituir el destino definitivo.
     */
    [[nodiscard]] RcloneStatus
    upload(const QString &localSrc,
           const QString &remoteDest,
           const UploadCallback &onProgress,
           const RcloneWriteOptions &options,
           const RcloneContext &ctx) const;

    /**
     * @brief Ejecuta una consulta específica de un backend.
     *
     * No debe utilizarse como sustituto de las primitivas tipadas de archivos.
     */
    [[nodiscard]] RcloneResponse<QByteArray>
    backendQuery(const QString &remoteName,
                 const QString &command,
                 const QStringList &args,
                 const RcloneContext &ctx) const;

private:
    /**
     * @brief Localiza el ejecutable de rclone.
     */
    [[nodiscard]] static QString locateExecutable();

    using OutputCallback =
        std::function<bool(const QByteArray &)>;

    /**
     * @brief Ejecuta un comando consumiendo stdout/stderr incrementalmente.
     */
    [[nodiscard]] RcloneStatus
    runStreamingCommand(const QStringList &arguments,
                        const OutputCallback &onOutput,
                        const RcloneContext &ctx,
                        const OutputCallback &onError = {},
                        int timeoutMs = 120000) const;

    /**
     * @brief Ejecuta un comando y captura completamente stdout.
     */
    [[nodiscard]] RcloneResponse<QByteArray>
    runCommand(const QStringList &arguments,
               const RcloneContext &ctx) const;

    QString m_executable;
};
