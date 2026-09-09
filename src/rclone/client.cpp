/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "client.h"

#include <utility>

#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>

namespace {

RcloneError notImplementedError()
{
    return {
        RcloneErrorCode::Unsupported,
        QStringLiteral("Operation not implemented"),
        -1,
    };
}

RcloneStatus notImplementedStatus()
{
    return {false, notImplementedError()};
}

template <typename T>
RcloneResponse<T> notImplementedResponse()
{
    return {std::nullopt, notImplementedError()};
}

} // namespace


RcloneClient::RcloneClient(QString executable)
    : m_executable(executable.isEmpty()
                       ? locateExecutable()
                       : std::move(executable))
{
}

QString RcloneClient::executable() const
{
    return m_executable;
}

bool RcloneClient::isAvailable() const
{
    if (m_executable.isEmpty()) {
        return false;
    }

    const QFileInfo executableInfo(m_executable);
    if (!executableInfo.isFile() || !executableInfo.isExecutable()) {
        return false;
    }

    QProcess process;
    process.setProgram(m_executable);
    process.setArguments({QStringLiteral("version")});
    process.start();

    if (!process.waitForStarted(5000)) {
        return false;
    }

    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }

    return process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
}

RcloneResponse<QList<RcloneRemote>>
RcloneClient::listRemotes(const RcloneContext &ctx) const
{
    const auto result = runCommand(
        {QStringLiteral("listremotes"), QStringLiteral("--json")},
        ctx);

    if (!result.success()) {
        return {std::nullopt, result.error};
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(*result.data, &parseError);

    if (parseError.error != QJsonParseError::NoError
        || !document.isArray()) {
        return {
            std::nullopt,
            {
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid remote list JSON: %1")
                    .arg(parseError.errorString()),
                -1,
            },
        };
    }

    QList<RcloneRemote> remotes;
    const QJsonArray array = document.array();

    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            return {
                std::nullopt,
                {
                    RcloneErrorCode::InvalidResponse,
                    QStringLiteral("Invalid remote entry at index %1").arg(i),
                    -1,
                },
            };
        }

        auto remote = RcloneRemote::fromJson(array.at(i).toObject());

        if (!remote) {
            return {
                std::nullopt,
                {
                    RcloneErrorCode::InvalidResponse,
                    QStringLiteral("Invalid remote entry at index %1").arg(i),
                    -1,
                },
            };
        }

        remotes.append(std::move(*remote));
    }

    return {
        std::move(remotes),
        {RcloneErrorCode::None, {}, 0},
    };
}

RcloneResponse<QByteArray>
RcloneClient::runConfigCommand(const QStringList &arguments,
                               const RcloneContext &ctx) const
{
    QStringList command{
        QStringLiteral("config"),
    };
    command.append(arguments);

    return runCommand(command, ctx);
}

RcloneResponse<RcloneSpace>
RcloneClient::spaceInfo(const QString &remoteSpec,
                        const RcloneContext &ctx) const
{
    const auto result = runCommand(
        {
            QStringLiteral("about"),
            remoteSpec,
            QStringLiteral("--json"),
        },
        ctx);

    if (!result.success()) {
        return {std::nullopt, result.error};
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(*result.data, &parseError);

    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return {
            std::nullopt,
            {
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid space info JSON: %1")
                    .arg(parseError.errorString()),
                -1,
            },
        };
    }

    auto space = RcloneSpace::fromJson(document.object());

    if (!space) {
        return {
            std::nullopt,
            {
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid space info values"),
                -1,
            },
        };
    }

    return {
        std::move(space),
        {RcloneErrorCode::None, {}, 0},
    };
}

RcloneResponse<RcloneItem>
RcloneClient::stat(const QString &remoteSpec,
                   const RcloneContext &ctx) const
{
    const auto result = runCommand(
        {
            QStringLiteral("lsjson"),
            remoteSpec,
            QStringLiteral("--stat"),
        },
        ctx);

    if (!result.success()) {
        return {
            std::nullopt,
            result.error,
        };
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(*result.data, &parseError);

    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return {
            std::nullopt,
            {
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid stat JSON: %1")
                    .arg(parseError.errorString()),
                -1,
            },
        };
    }

    auto item = RcloneItem::fromJson(document.object());

    if (!item) {
        return {
            std::nullopt,
            {
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid stat response"),
                -1,
            },
        };
    }

    return {
        std::move(item),
        {RcloneErrorCode::None, {}, 0},
    };
}

RcloneStatus
RcloneClient::list(const QString &remoteSpec,
                   bool recursive,
                   const ItemCallback &onItem,
                   const RcloneContext &ctx) const
{
    RcloneListOptions options;
    options.recursive = recursive;

    return list(remoteSpec, options, onItem, ctx);
}

RcloneStatus
RcloneClient::list(const QString &remoteSpec,
                   const RcloneListOptions &options,
                   const ItemCallback &onItem,
                   const RcloneContext &ctx) const
{
    QStringList arguments{
        QStringLiteral("lsjson"),
        remoteSpec,
    };

    if (options.recursive) {
        arguments.append(QStringLiteral("--recursive"));
    }

    arguments.append(options.extraArguments);

    QByteArray pending;
    bool arrayStarted = false;
    bool arrayFinished = false;

    auto failure = [](RcloneErrorCode code,
                      const QString &message) -> RcloneStatus {
        return {false, {code, message, -1}};
    };

    auto processLine = [&](QByteArray line) -> RcloneStatus {
        line = line.trimmed();

        if (line.isEmpty()) {
            return {true, {RcloneErrorCode::None, {}, 0}};
        }

        if (!arrayStarted) {
            if (line != "[") {
                return failure(
                    RcloneErrorCode::InvalidResponse,
                    QStringLiteral("Expected JSON array"));
            }

            arrayStarted = true;
            return {true, {RcloneErrorCode::None, {}, 0}};
        }

        if (arrayFinished) {
            return failure(
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Unexpected data after JSON array"));
        }

        if (line == "]") {
            arrayFinished = true;
            return {true, {RcloneErrorCode::None, {}, 0}};
        }

        // Los elementos del array pueden terminar con una coma.
        if (line.endsWith(',')) {
            line.chop(1);
            line = line.trimmed();
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(line, &parseError);

        if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
            return failure(
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid listing item JSON: %1")
                    .arg(parseError.errorString()));
        }

        auto item = RcloneItem::fromJson(document.object());

        if (!item) {
            return failure(
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid listing item"));
        }

        if (!onItem(*item)) {
            return failure(
                RcloneErrorCode::Aborted,
                QStringLiteral("Listing aborted by consumer"));
        }

        return {true, {RcloneErrorCode::None, {}, 0}};
    };

    RcloneStatus parseStatus{
        true,
        {RcloneErrorCode::None, {}, 0},
    };

    const auto status = runStreamingCommand(
        arguments,
        [&](const QByteArray &chunk) {
            pending += chunk;

            while (true) {
                const qsizetype newline = pending.indexOf('\n');

                if (newline < 0) {
                    break;
                }

                QByteArray line = pending.left(newline);
                pending.remove(0, newline + 1);

                parseStatus = processLine(std::move(line));

                if (!parseStatus.success()) {
                    return false;
                }
            }

            return true;
        },
        ctx);

    /*
     * Si el parser o el consumidor detuvo el streaming, conservamos
     * su error específico en lugar del Aborted genérico del helper.
     */
    if (!parseStatus.success()) {
        return parseStatus;
    }

    if (!status.success()) {
        return status;
    }

    // Procesa la última línea si rclone terminó sin '\n'.
    if (!pending.trimmed().isEmpty()) {
        parseStatus = processLine(std::move(pending));

        if (!parseStatus.success()) {
            return parseStatus;
        }
    }

    if (!arrayStarted || !arrayFinished) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("Incomplete listing JSON"));
    }

    return {
        true,
        {RcloneErrorCode::None, {}, 0},
    };
}

RcloneStatus
RcloneClient::mkdir(const QString &remoteSpec,
                    const RcloneContext &ctx) const
{
    Q_UNUSED(remoteSpec)
    Q_UNUSED(ctx)

    // TODO: Crear el directorio remoto.
    return notImplementedStatus();
}

RcloneStatus
RcloneClient::remove(const QString &remoteSpec,
                     RcloneRemovalMode mode,
                     const RcloneContext &ctx) const
{
    Q_UNUSED(remoteSpec)
    Q_UNUSED(mode)
    Q_UNUSED(ctx)

    // TODO: Eliminar el elemento respetando la política de recursividad.
    return notImplementedStatus();
}

RcloneStatus
RcloneClient::move(const QString &srcSpec,
                   const QString &destSpec,
                   const RcloneWriteOptions &options,
                   const RcloneContext &ctx) const
{
    Q_UNUSED(srcSpec)
    Q_UNUSED(destSpec)
    Q_UNUSED(options)
    Q_UNUSED(ctx)

    // TODO: Mover o renombrar respetando la política de sobrescritura.
    return notImplementedStatus();
}

RcloneStatus
RcloneClient::copy(const QString &srcSpec,
                   const QString &destSpec,
                   const RcloneWriteOptions &options,
                   const RcloneContext &ctx) const
{
    Q_UNUSED(srcSpec)
    Q_UNUSED(destSpec)
    Q_UNUSED(options)
    Q_UNUSED(ctx)

    // TODO: Copiar respetando la política de sobrescritura.
    return notImplementedStatus();
}

RcloneStatus
RcloneClient::read(const QString &remoteSpec,
                   const DownloadCallback &onChunk,
                   const RcloneContext &ctx) const
{
    Q_UNUSED(remoteSpec)
    Q_UNUSED(onChunk)
    Q_UNUSED(ctx)

    // TODO: Descargar mediante streaming y entregar bloques al callback.
    return notImplementedStatus();
}

RcloneStatus
RcloneClient::write(const QString &localSrc,
                    const QString &remoteDest,
                    const UploadCallback &onProgress,
                    const RcloneWriteOptions &options,
                    const RcloneContext &ctx) const
{
    Q_UNUSED(localSrc)
    Q_UNUSED(remoteDest)
    Q_UNUSED(onProgress)
    Q_UNUSED(options)
    Q_UNUSED(ctx)

    // TODO: Subir el archivo local y emitir telemetría de progreso.
    return notImplementedStatus();
}

QString RcloneClient::locateExecutable()
{
    const QByteArray configuredPath = qgetenv("KIO_RCLONE_EXECUTABLE");
    if (!configuredPath.isEmpty()) {
        const QString configured = QString::fromLocal8Bit(configuredPath);
        const QString resolved = QStandardPaths::findExecutable(configured);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    return QStandardPaths::findExecutable(QStringLiteral("rclone"));
}

RcloneStatus
RcloneClient::runStreamingCommand(const QStringList &arguments,
                                  const OutputCallback &onOutput,
                                  const RcloneContext &ctx) const
{
    if (m_executable.isEmpty()) {
        return {
            false,
            {
                RcloneErrorCode::ProcessFailure,
                QStringLiteral("Rclone executable not found"),
                -1,
            },
        };
    }

    if (ctx.isCancelled()) {
        return {
            false,
            {
                RcloneErrorCode::Cancelled,
                QStringLiteral("Operation cancelled"),
                -1,
            },
        };
    }

    constexpr int pollIntervalMs = 100;
    constexpr int startupTimeoutMs = 5000;
    constexpr int timeoutMs = 120000;

    QProcess process;
    process.setProgram(m_executable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QByteArray standardError;

    auto stopProcess = [&]() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    };

    auto fail = [](RcloneErrorCode code,
                   const QString &message,
                   int exitCode = -1) -> RcloneStatus {
        return {
            false,
            {
                code,
                message,
                exitCode,
            },
        };
    };

    auto drainOutput = [&]() -> bool {
        const QByteArray output = process.readAllStandardOutput();

        if (!output.isEmpty()) {
            if (!onOutput(output)) {
                return false;
            }
        }

        standardError += process.readAllStandardError();

        return true;
    };

    QElapsedTimer timer;
    timer.start();

    process.start();

    /*
     * Espera a que el proceso arranque sin bloquear durante todo el
     * startup timeout, de modo que podamos observar la cancelación.
     */
    while (process.state() == QProcess::Starting) {
        if (ctx.isCancelled()) {
            stopProcess();

            return fail(
                RcloneErrorCode::Cancelled,
                QStringLiteral("Operation cancelled"));
        }

        if (timer.elapsed() >= startupTimeoutMs) {
            stopProcess();

            return fail(
                RcloneErrorCode::TimedOut,
                QStringLiteral("Rclone startup timed out"));
        }

        process.waitForStarted(pollIntervalMs);
    }

    if (process.state() == QProcess::NotRunning
        && process.error() == QProcess::FailedToStart) {
        return fail(
            RcloneErrorCode::ProcessFailure,
            process.errorString());
    }

    /*
     * Consume stdout mientras rclone continúa ejecutándose.
     *
     * No esperamos a que el proceso termine para entregar los datos:
     * cada bloque disponible se propaga inmediatamente a onOutput().
     */
    while (process.state() != QProcess::NotRunning) {
        if (!drainOutput()) {
            stopProcess();

            return fail(
                RcloneErrorCode::Aborted,
                QStringLiteral("Operation aborted by consumer"));
        }

        if (ctx.isCancelled()) {
            stopProcess();

            return fail(
                RcloneErrorCode::Cancelled,
                QStringLiteral("Operation cancelled"));
        }

        if (timer.elapsed() >= timeoutMs) {
            stopProcess();

            return fail(
                RcloneErrorCode::TimedOut,
                QStringLiteral("Operation timed out"));
        }

        /*
         * Esperamos un intervalo corto. QProcess continúa llenando sus
         * buffers internos y en la siguiente iteración drenamos stdout
         * y stderr nuevamente.
         */
        process.waitForReadyRead(pollIntervalMs);
    }

    /*
     * QProcess puede conservar bytes después de que el proceso ya haya
     * terminado. Hay que entregarlos antes de evaluar el exit code.
     */
    if (!drainOutput()) {
        return fail(
            RcloneErrorCode::Aborted,
            QStringLiteral("Operation aborted by consumer"));
    }

    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        QString message =
            QString::fromUtf8(standardError).trimmed();

        if (message.isEmpty()) {
            message = process.errorString();
        }

        return fail(
            RcloneErrorCode::ProcessFailure,
            message,
            process.exitCode());
    }

    return {
        true,
        {
            RcloneErrorCode::None,
            {},
            0,
        },
    };
}

RcloneResponse<QByteArray>
RcloneClient::runCommand(const QStringList &arguments,
                         const RcloneContext &ctx) const
{
    QByteArray output;

    const RcloneStatus status =
        runStreamingCommand(
            arguments,
            [&](const QByteArray &chunk) {
                output += chunk;
                return true;
            },
            ctx);

    if (!status.success()) {
        return {
            std::nullopt,
            status.error,
        };
    }

    return {
        std::move(output),
        {
            RcloneErrorCode::None,
            {},
            0,
        },
    };
}

RcloneResponse<QByteArray>
RcloneClient::backendQuery(const QString &remoteName,
                           const QString &command,
                           const QStringList &args,
                           const RcloneContext &ctx) const
{
    QStringList arguments{
        QStringLiteral("backend"),
        command,
        remoteName,
    };
    arguments.append(args);

    return runCommand(arguments, ctx);
}
