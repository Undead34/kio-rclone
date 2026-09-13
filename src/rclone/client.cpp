/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "client.h"
#include "clienthelpers.h"

#include <utility>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

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
    return stat(remoteSpec, RcloneListOptions{}, ctx);
}

RcloneResponse<RcloneItem>
RcloneClient::stat(const QString &remoteSpec,
                   const RcloneListOptions &options,
                   const RcloneContext &ctx) const
{
    QStringList arguments{
        QStringLiteral("lsjson"),
        remoteSpec,
        QStringLiteral("--stat"),
    };
    arguments.append(options.extraArguments);

    const auto result = runCommand(arguments, ctx);

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
RcloneClient::mkdir(const QString &remoteSpec,
                    const RcloneContext &ctx) const
{
    return runStreamingCommand(
        {
            QStringLiteral("mkdir"),
            remoteSpec,
        },
        [](const QByteArray &) {
            return true;
        },
        ctx);
}

RcloneStatus
RcloneClient::copy(const QString &srcSpec,
                   const QString &destSpec,
                   const RcloneWriteOptions &options,
                   const RcloneContext &ctx) const
{
    auto failure = [](RcloneErrorCode code,
                      const QString &message) -> RcloneStatus {
        return {false, {code, message, -1}};
    };

    if (ctx.isCancelled()) {
        return failure(
            RcloneErrorCode::Cancelled,
            QStringLiteral("Operation cancelled"));
    }

    // Copiar un elemento sobre sí mismo no es una operación válida.
    if (srcSpec == destSpec) {
        return failure(
            RcloneErrorCode::AlreadyExists,
            QStringLiteral("Source and destination are the same"));
    }

    const auto destination = stat(destSpec, ctx);

    if (destination.success()) {
        if (!options.replaceExisting) {
            return failure(
                RcloneErrorCode::AlreadyExists,
                QStringLiteral("Destination already exists"));
        }

        // copyto puede combinar directorios. No lo tratamos como
        // un reemplazo simple.
        const auto source = stat(srcSpec, ctx);

        if (!source.success()) {
            return {false, source.error};
        }

        if (source.data->isDirectory
            || destination.data->isDirectory) {
            return failure(
                RcloneErrorCode::Unsupported,
                QStringLiteral(
                    "Replacing an existing directory is not supported"));
        }
    } else if (destination.error.code != RcloneErrorCode::NotFound) {
        // Un error de permisos, red o parsing no significa que
        // el destino esté libre.
        return {false, destination.error};
    }

    return runStreamingCommand(
        {
            QStringLiteral("copyto"),
            srcSpec,
            destSpec,
            QStringLiteral("--no-traverse"),
        },
        [](const QByteArray &) {
            return true;
        },
        ctx);
}

RcloneStatus
RcloneClient::move(const QString &srcSpec,
                   const QString &destSpec,
                   const RcloneWriteOptions &options,
                   const RcloneContext &ctx) const
{
    auto failure = [](RcloneErrorCode code,
                      const QString &message) -> RcloneStatus {
        return {false, {code, message, -1}};
    };

    if (ctx.isCancelled()) {
        return failure(
            RcloneErrorCode::Cancelled,
            QStringLiteral("Operation cancelled"));
    }

    if (srcSpec == destSpec) {
        return {true, {RcloneErrorCode::None, {}, 0}};
    }

    // No usamos --no-check-dest: necesitamos respetar replaceExisting.
    const auto destination = stat(destSpec, ctx);

    if (destination.success()) {
        if (!options.replaceExisting) {
            return failure(
                RcloneErrorCode::AlreadyExists,
                QStringLiteral("Destination already exists"));
        }

        // moveto sobre un directorio existente puede combinar contenido.
        // No lo tratamos como un reemplazo simple.
        const auto source = stat(srcSpec, ctx);

        if (!source.success()) {
            return {false, source.error};
        }

        if (source.data->isDirectory
            || destination.data->isDirectory) {
            return failure(
                RcloneErrorCode::Unsupported,
                QStringLiteral(
                    "Replacing an existing directory is not supported"));
        }
    } else if (destination.error.code != RcloneErrorCode::NotFound) {
        // Un error de red, permisos o parsing NO significa que el
        // destino esté libre.
        return {false, destination.error};
    }

    return runStreamingCommand(
        {
            QStringLiteral("moveto"),
            srcSpec,
            destSpec,
            QStringLiteral("--no-traverse"),
        },
        [](const QByteArray &) {
            return true;
        },
        ctx);
}

RcloneStatus
RcloneClient::download(const QString &remoteSpec,
                       const DownloadCallback &onChunk,
                       const RcloneContext &ctx) const
{
    return runStreamingCommand(
        {
            QStringLiteral("cat"),
            remoteSpec,
        },
        onChunk,
        ctx);
}

RcloneStatus
RcloneClient::upload(const QString &localSrc,
                     const QString &remoteDest,
                     const UploadCallback &onProgress,
                     const RcloneWriteOptions &options,
                     const RcloneContext &ctx) const
{
    auto failure = [](RcloneErrorCode code,
                      const QString &message) -> RcloneStatus {
        return {false, {code, message, -1}};
    };

    if (ctx.isCancelled()) {
        return failure(
            RcloneErrorCode::Cancelled,
            QStringLiteral("Operation cancelled"));
    }

    const QFileInfo source(localSrc);

    if (!source.isFile()) {
        return failure(
            RcloneErrorCode::NotFound,
            QStringLiteral("Local source is not a regular file"));
    }

    if (!source.isReadable()) {
        return failure(
            RcloneErrorCode::PermissionDenied,
            QStringLiteral("Local source is not readable"));
    }

    RcloneTargetSnapshot expected;

    if (options.expectedTarget) {
        expected = *options.expectedTarget;
    } else {
        // Do not confuse a failed stat with an available destination.
        const auto destination = stat(remoteDest, ctx);

        if (destination.success()) {
            expected =
                RcloneTargetSnapshot::fromItem(*destination.data);
        } else if (destination.error.code != RcloneErrorCode::NotFound) {
            return {false, destination.error};
        }
    }

    if (expected.exists) {
        if (!options.replaceExisting) {
            return failure(
                RcloneErrorCode::AlreadyExists,
                QStringLiteral("Destination already exists"));
        }

        if (expected.isDirectory) {
            return failure(
                RcloneErrorCode::Unsupported,
                QStringLiteral(
                    "Replacing an existing directory is not supported"));
        }
    }

    const qint64 fileSize = source.size();
    const QString stagingSpec = RcloneClientHelpers::uploadStagingSpec(remoteDest);

    auto cleanupStaging = [this, &stagingSpec]() {
        RcloneClientHelpers::CleanupContext cleanupContext;
        (void)runStreamingCommand(
            {
                QStringLiteral("deletefile"),
                stagingSpec,
            },
            [](const QByteArray &) {
                return true;
            },
            cleanupContext,
            {},
            30000);
    };

    TransferStats progress;

    if (onProgress) {
        onProgress(progress);
    }

    QByteArray pendingError;

    const RcloneStatus status = runStreamingCommand(
        {
            QStringLiteral("copyto"),
            localSrc,
            stagingSpec,
            QStringLiteral("--no-traverse"),
            QStringLiteral("--use-json-log"),
            QStringLiteral("--stats"),
            QStringLiteral("1s"),
            QStringLiteral("--stats-log-level"),
            QStringLiteral("NOTICE"),
            QStringLiteral("--log-level"),
            QStringLiteral("NOTICE"),
        },
        [](const QByteArray &) {
            return true;
        },
        ctx,
        [&](const QByteArray &chunk) {
            pendingError += chunk;

            while (true) {
                const qsizetype newline =
                    pendingError.indexOf('\n');

                if (newline < 0) {
                    break;
                }

                QByteArray line = pendingError.left(newline);
                pendingError.remove(0, newline + 1);

                QJsonParseError parseError;
                const QJsonDocument document =
                    QJsonDocument::fromJson(line, &parseError);

                if (parseError.error != QJsonParseError::NoError
                    || !document.isObject()) {
                    // Puede ser un diagnóstico que no sea JSON.
                    continue;
                }

                const QJsonValue statsValue =
                    document.object().value(QStringLiteral("stats"));

                if (!statsValue.isObject()) {
                    continue;
                }

                RcloneClientHelpers::updateTransferStats(
                    statsValue.toObject(),
                    fileSize,
                    progress);

                if (onProgress) {
                    onProgress(progress);
                }
            }

            return true;
        },
        -1);

    if (!status.success()) {
        cleanupStaging();
        return status;
    }

    // Validate after the potentially long transfer. The fully uploaded sibling
    // can be discarded without ever exposing partial contents at remoteDest.
    const RcloneStatus unchanged = RcloneClientHelpers::targetStillMatches(
        expected,
        stat(remoteDest, ctx));

    if (!unchanged.success()) {
        cleanupStaging();
        return unchanged;
    }

    const RcloneStatus published = runStreamingCommand(
        {
            QStringLiteral("moveto"),
            stagingSpec,
            remoteDest,
            QStringLiteral("--no-traverse"),
            QStringLiteral("--ignore-times"),
        },
        [](const QByteArray &) {
            return true;
        },
        ctx,
        {},
        -1);

    if (!published.success()) {
        cleanupStaging();
        return published;
    }

    // Publication is complete even when the last stats interval did not fire.
    progress.bytes = fileSize;
    progress.percentage = 100;
    progress.speed = 0;
    progress.etaSeconds = 0;

    if (onProgress) {
        onProgress(progress);
    }

    return published;
}


RcloneStatus
RcloneClient::remove(const QString &remoteSpec,
                     RcloneRemovalMode mode,
                     const RcloneContext &ctx) const
{
    QString command;

    switch (mode) {
    case RcloneRemovalMode::File:
        command = QStringLiteral("deletefile");
        break;

    case RcloneRemovalMode::EmptyDirectory:
        command = QStringLiteral("rmdir");
        break;

    case RcloneRemovalMode::Recursive:
        command = QStringLiteral("purge");
        break;
    }

    return runStreamingCommand(
        {
            command,
            remoteSpec,
        },
        [](const QByteArray &) {
            return true;
        },
        ctx);
}

RcloneStatus
RcloneClient::setModificationTime(const QString &remoteSpec,
                                  const QDateTime &mtime,
                                  const RcloneContext &ctx) const
{
    if (!mtime.isValid()) {
        return {
            false,
            {
                RcloneErrorCode::InvalidResponse,
                QStringLiteral("Invalid modification time"),
                -1,
            },
        };
    }

    /*
     * rclone interpreta --timestamp como UTC. --no-create evita que una
     * petición de KIO para un objeto que ya desapareció cree un archivo vacío.
     */
    const QString timestamp = mtime.toUTC().toString(
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));

    return runStreamingCommand(
        {
            QStringLiteral("touch"),
            QStringLiteral("--no-create"),
            QStringLiteral("--timestamp"),
            timestamp,
            remoteSpec,
        },
        [](const QByteArray &) {
            return true;
        },
        ctx);
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
                                  const RcloneContext &ctx,
                                  const OutputCallback &onError,
                                  int timeoutMs) const
{
    auto fail = [](RcloneErrorCode code,
                   const QString &message,
                   int exitCode = -1) -> RcloneStatus {
        return {false, {code, message, exitCode}};
    };

    if (m_executable.isEmpty()) {
        return fail(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("Rclone executable not found"));
    }

    if (ctx.isCancelled()) {
        return fail(
            RcloneErrorCode::Cancelled,
            QStringLiteral("Operation cancelled"));
    }

    constexpr int pollIntervalMs = 100;
    constexpr int startupTimeoutMs = 5000;
    constexpr qsizetype maxErrorBytes = 64 * 1024;

    QProcess process;
    process.setProgram(m_executable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);

    QByteArray standardError;

    auto stopProcess = [&]() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    };

    auto drainOutput = [&]() -> bool {
        const QByteArray output = process.readAllStandardOutput();
        const QByteArray error = process.readAllStandardError();

        if (!error.isEmpty()) {
            // Conservamos solamente una cantidad acotada para diagnóstico.
            if (standardError.size() < maxErrorBytes) {
                standardError += error.left(
                    maxErrorBytes - standardError.size());
            }

            if (onError && !onError(error)) {
                return false;
            }
        }

        if (!output.isEmpty() && !onOutput(output)) {
            return false;
        }

        return true;
    };

    QElapsedTimer timer;
    timer.start();

    process.start();

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

    while (true) {
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

        if (process.state() == QProcess::NotRunning) {
            break;
        }

        if (timeoutMs >= 0 && timer.elapsed() >= timeoutMs) {
            stopProcess();
            return fail(
                RcloneErrorCode::TimedOut,
                QStringLiteral("Operation timed out"));
        }

        // No dependemos de que stdout produzca datos.
        process.waitForFinished(pollIntervalMs);
    }

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

        const int exitCode = process.exitCode();
        const RcloneErrorCode errorCode =
            process.exitStatus() == QProcess::NormalExit
            ? RcloneClientHelpers::classifyProcessFailure(exitCode, message)
            : RcloneErrorCode::ProcessFailure;

        return fail(errorCode, message, exitCode);
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
