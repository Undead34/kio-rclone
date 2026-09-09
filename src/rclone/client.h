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
 * @brief Error de una operación del cliente.
 *
 * message contiene el diagnóstico disponible. Las capas superiores deben
 * utilizar code para tomar decisiones, no comparar fragmentos de message.
 */
struct RcloneError {
    RcloneErrorCode code = RcloneErrorCode::Unknown;
    QString message;
    int exitCode = -1;
};

/**
 * @brief Resultado de una operación que produce un valor.
 *
 * Éxito: data contiene un valor y error.code es None.
 * Fallo: data está vacío y error describe la causa.
 */
template <typename T>
struct RcloneResponse {
    std::optional<T> data;
    RcloneError error;

    [[nodiscard]] bool success() const { return data.has_value(); }
};

/**
 * @brief Resultado de una operación que no produce un valor.
 *
 * Éxito: ok es true y error.code es None.
 * Fallo: ok es false y error describe la causa.
 */
struct RcloneStatus {
    bool ok = false;
    RcloneError error;

    [[nodiscard]] bool success() const { return ok; }
};

/**
 * @brief Contexto de cancelación proporcionado por el consumidor.
 *
 * El cliente consulta isCancelled() durante las operaciones bloqueantes.
 * El contexto debe permanecer vivo hasta que la operación termine.
 */
class RcloneContext
{
public:
    virtual ~RcloneContext() = default;

    [[nodiscard]] virtual bool isCancelled() const = 0;
};


/**
 * @brief Política de eliminación.
 */
enum class RcloneRemovalMode {
    File,
    EmptyDirectory,
    Recursive,
};

/**
 * @brief Política de destino para operaciones que crean o reemplazan archivos.
 *
 * Por defecto no se permite reemplazar un destino existente.
 */
struct RcloneWriteOptions {
    bool replaceExisting = false;
};

struct RcloneListOptions {
    bool recursive = false;
    QStringList extraArguments;
};

/**
 * @brief Cliente bloqueante para operaciones de archivos mediante rclone.
 *
 * Aísla la ejecución de procesos, el parsing de sus respuestas y la
 * traducción de errores. No depende de KIO ni de detalles de su Worker.
 *
 * Las operaciones son síncronas. La implementación debe aplicar una
 * política de timeout adecuada y consultar el contexto de cancelación
 * mientras espera al proceso.
 */
class RcloneClient
{
public:
    explicit RcloneClient(QString executable = {});

    /**
     * @brief Ruta del ejecutable de rclone que utilizará el cliente.
     */
    [[nodiscard]] QString executable() const;

    /**
     * @brief Comprueba si el ejecutable de rclone está disponible.
     *
     * No comprueba la conectividad de ningún remoto.
     */
    [[nodiscard]] bool isAvailable() const;

    /**
     * @brief Obtiene los remotos configurados y sus tipos.
     */
    [[nodiscard]] RcloneResponse<QList<RcloneRemote>>
    listRemotes(const RcloneContext &ctx) const;

    /**
     * @brief Ejecuta un subcomando de `rclone config`.
     *
     * La aplicación de configuración utiliza esta entrada para consultas y
     * cambios no interactivos. Las operaciones de archivos del worker deben
     * usar los métodos tipados del cliente.
     */
    [[nodiscard]] RcloneResponse<QByteArray>
    runConfigCommand(const QStringList &arguments,
                     const RcloneContext &ctx) const;

    /**
     * @brief Obtiene las métricas de espacio de un remoto.
     *
     * Algunos backends no soportan esta operación.
     */
    [[nodiscard]] RcloneResponse<RcloneSpace>
    spaceInfo(const QString &remoteSpec,
              const RcloneContext &ctx) const;

    /**
     * @brief Obtiene los metadatos de un archivo o directorio.
     */
    [[nodiscard]] RcloneResponse<RcloneItem>
    stat(const QString &remoteSpec,
         const RcloneContext &ctx) const;

    /**
     * @brief Obtiene metadatos respetando los filtros de una vista de rclone.
     */
    [[nodiscard]] RcloneResponse<RcloneItem>
    stat(const QString &remoteSpec,
         const RcloneListOptions &options,
         const RcloneContext &ctx) const;

    using ItemCallback = std::function<bool(const RcloneItem &)>;

    /**
     * @brief Lista un directorio de forma incremental.
     *
     * @param onItem Se invoca por cada elemento.
     * @return Aborted si el callback devuelve false; Cancelled si el
     *         contexto solicita cancelación. Un listado completo devuelve éxito.
     *
     * Los elementos ya emitidos no se deshacen si la operación falla.
     */
    [[nodiscard]] RcloneStatus
    list(const QString &remoteSpec,
         bool recursive,
         const ItemCallback &onItem,
         const RcloneContext &ctx) const;

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
     * @brief Cambia la fecha de modificación de un archivo o directorio remoto.
     *
     * La operación no crea el destino cuando no existe.
     */
    [[nodiscard]] RcloneStatus
    setModificationTime(const QString &remoteSpec,
                        const QDateTime &mtime,
                        const RcloneContext &ctx) const;

    /**
     * @brief Elimina un archivo o directorio.
     *
     * EmptyDirectory no elimina recursivamente un directorio que contiene
     * elementos.
     */
    [[nodiscard]] RcloneStatus
    remove(const QString &remoteSpec,
           RcloneRemovalMode mode,
           const RcloneContext &ctx) const;

    /**
     * @brief Mueve o renombra un elemento remoto.
     *
     * El destino existente solo puede reemplazarse si options lo permite.
     */
    [[nodiscard]] RcloneStatus
    move(const QString &srcSpec,
         const QString &destSpec,
         const RcloneWriteOptions &options,
         const RcloneContext &ctx) const;

    /**
     * @brief Copia un elemento remoto.
     *
     * El destino existente solo puede reemplazarse si options lo permite.
     */
    [[nodiscard]] RcloneStatus
    copy(const QString &srcSpec,
         const QString &destSpec,
         const RcloneWriteOptions &options,
         const RcloneContext &ctx) const;

    using DownloadCallback = std::function<bool(const QByteArray &chunk)>;

    /**
     * @brief Descarga un archivo remoto mediante streaming.
     *
     * Entrega bloques conforme rclone los produce. No proporciona
     * lectura aleatoria ni crea una copia local por sí mismo.
     */
    [[nodiscard]] RcloneStatus
    download(const QString &remoteSpec,
             const DownloadCallback &onChunk,
             const RcloneContext &ctx) const;

    [[nodiscard]] RcloneStatus
    downloadRange(const QString &remoteSpec,
                  qint64 offset,
                  qint64 size,
                  const DownloadCallback &onChunk,
                  const RcloneContext &ctx) const;

    /**
     * @brief Telemetría de una transferencia.
     *
     * Los valores desconocidos se representan con -1.
     * speed se expresa en bytes por segundo.
     */
    struct TransferStats {
        int percentage = -1;
        qint64 bytes = 0;
        qint64 speed = -1;
        qint64 etaSeconds = -1;
    };

    using UploadCallback = std::function<void(const TransferStats &)>;

    /**
     * @brief Sube un archivo local a un destino remoto.
     *
     * La implementación procesa la telemetría de rclone y notifica el
     * progreso mediante onProgress. No recibe datos directamente de KIO.
     */
    [[nodiscard]] RcloneStatus
    upload(const QString &localSrc,
           const QString &remoteDest,
           const UploadCallback &onProgress,
           const RcloneWriteOptions &options,
           const RcloneContext &ctx) const;

    /**
     * @brief Ejecuta una consulta nativa específica de un backend.
     *
     * Ejemplo: una consulta de unidades compartidas de Google Drive.
     * Devuelve la salida estándar sin imponer un modelo específico
     * del proveedor.
     *
     * No está destinado a ejecutar operaciones generales de archivos.
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

    using OutputCallback = std::function<bool(const QByteArray &)>;

    [[nodiscard]] RcloneStatus
    runStreamingCommand(const QStringList &arguments,
                        const OutputCallback &onOutput,
                        const RcloneContext &ctx,
                        const OutputCallback &onError = {},
                        int timeoutMs = 120000) const;

    [[nodiscard]] RcloneResponse<QByteArray>
    runCommand(const QStringList &arguments,
               const RcloneContext &ctx) const;

    QString m_executable;
};
