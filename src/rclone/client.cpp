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
    QStringList arguments{
        QStringLiteral("lsjson"),
        remoteSpec,
    };

    if (recursive) {
        arguments.append(QStringLiteral("--recursive"));
    }

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
    return QStandardPaths::findExecutable(QStringLiteral("rclone"));
}

#include <QElapsedTimer>
#include <QProcess>

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


// /*
//  * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
//  *
//  * SPDX-License-Identifier: GPL-2.0-or-later
//  */

// #include "client.h"
// #include "process.h"

// #include <QElapsedTimer>
// #include <QFileInfo>
// #include <QJsonArray>
// #include <QJsonDocument>
// #include <QJsonObject>
// #include <QProcess>
// #include <QStandardPaths>

// #include <algorithm>
// #include <utility>

// namespace
// {
// constexpr qsizetype MaximumListingItemSize = 4 * 1024 * 1024;

// std::optional<RcloneItem> itemFromObject(const QJsonObject &object)
// {
//     if (object.isEmpty()) {
//         return std::nullopt;
//     }

//     RcloneItem item;
//     item.name = object.value(QStringLiteral("Name")).toString();
//     item.path = object.value(QStringLiteral("Path")).toString();
//     item.id = object.value(QStringLiteral("ID")).toString();
//     item.mimeType = object.value(QStringLiteral("MimeType")).toString();
//     item.size = object.value(QStringLiteral("Size")).toVariant().toLongLong();
//     item.isDirectory = object.value(QStringLiteral("IsDir")).toBool();
//     item.readOnly = !item.isDirectory && item.size < 0;
//     item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODateWithMs);
//     if (!item.modificationTime.isValid()) {
//         item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODate);
//     }

//     if (item.name.isEmpty() && !item.path.isEmpty()) {
//         item.name = item.path.section(QLatin1Char('/'), -1);
//     }
//     return item;
// }

// void setError(QString *error, const QString &message)
// {
//     if (error) {
//         *error = message;
//     }
// }

// bool isJsonWhitespace(char value)
// {
//     return value == ' ' || value == '\n' || value == '\r' || value == '\t';
// }

// // lsjson writes one JSON object at a time, but its output is still a valid JSON
// // array.  Parse the array incrementally instead of assuming a line layout: a
// // filename may legally contain escaped newlines or braces.
// class JsonArrayItemReader
// {
//   public:
//     explicit JsonArrayItemReader(RcloneClient::ItemCallback onItem)
//         : m_onItem(std::move(onItem))
//     {
//     }

//     bool feed(const QByteArray &data, QString *error)
//     {
//         for (const char character : data) {
//             if (!consume(character, error)) {
//                 return false;
//             }
//         }
//         return true;
//     }

//     bool finish(QString *error) const
//     {
//         if (m_state == State::Finished) {
//             return true;
//         }
//         setError(error, QStringLiteral("rclone returned an incomplete JSON listing"));
//         return false;
//     }

//   private:
//     enum class State {
//         BeforeArray,
//         BeforeItem,
//         InItem,
//         AfterItem,
//         Finished,
//     };

//     bool fail(QString *error, const QString &message) const
//     {
//         setError(error, message);
//         return false;
//     }

//     bool completeItem(QString *error)
//     {
//         QJsonParseError parseError;
//         const QJsonDocument document = QJsonDocument::fromJson(m_item, &parseError);
//         if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
//             return fail(error, QStringLiteral("Invalid item in rclone listing: %1").arg(parseError.errorString()));
//         }

//         const std::optional<RcloneItem> item = itemFromObject(document.object());
//         if (!item) {
//             return fail(error, QStringLiteral("rclone returned an empty listing item"));
//         }
//         if (!m_onItem(*item)) {
//             return fail(error, QStringLiteral("Listing was stopped by the caller"));
//         }

//         m_item.clear();
//         m_state = State::AfterItem;
//         return true;
//     }

//     bool consume(char character, QString *error)
//     {
//         switch (m_state) {
//         case State::BeforeArray:
//             if (isJsonWhitespace(character)) {
//                 return true;
//             }
//             if (character != '[') {
//                 return fail(error, QStringLiteral("rclone listing is not a JSON array"));
//             }
//             m_state = State::BeforeItem;
//             m_allowArrayEnd = true;
//             return true;

//         case State::BeforeItem:
//             if (isJsonWhitespace(character)) {
//                 return true;
//             }
//             if (character == ']') {
//                 return m_allowArrayEnd ? finishArray() : fail(error, QStringLiteral("Unexpected end of rclone listing"));
//             }
//             if (character != '{') {
//                 return fail(error, QStringLiteral("rclone listing contains a non-object item"));
//             }
//             m_item = QByteArray(1, character);
//             m_objectDepth = 1;
//             m_inString = false;
//             m_escaped = false;
//             m_state = State::InItem;
//             return true;

//         case State::InItem:
//             if (m_item.size() >= MaximumListingItemSize) {
//                 return fail(error, QStringLiteral("A single rclone listing item is too large to process safely"));
//             }
//             m_item.append(character);
//             if (m_inString) {
//                 if (m_escaped) {
//                     m_escaped = false;
//                 } else if (character == '\\') {
//                     m_escaped = true;
//                 } else if (character == '"') {
//                     m_inString = false;
//                 }
//                 return true;
//             }

//             if (character == '"') {
//                 m_inString = true;
//             } else if (character == '{') {
//                 ++m_objectDepth;
//             } else if (character == '}') {
//                 --m_objectDepth;
//                 if (m_objectDepth < 0) {
//                     return fail(error, QStringLiteral("Invalid object nesting in rclone listing"));
//                 }
//                 if (m_objectDepth == 0) {
//                     return completeItem(error);
//                 }
//             }
//             return true;

//         case State::AfterItem:
//             if (isJsonWhitespace(character)) {
//                 return true;
//             }
//             if (character == ',') {
//                 m_state = State::BeforeItem;
//                 m_allowArrayEnd = false;
//                 return true;
//             }
//             if (character == ']') {
//                 return finishArray();
//             }
//             return fail(error, QStringLiteral("Missing separator in rclone listing"));

//         case State::Finished:
//             return isJsonWhitespace(character) ? true : fail(error, QStringLiteral("Unexpected data after rclone listing"));
//         }

//         return fail(error, QStringLiteral("Invalid rclone listing parser state"));
//     }

//     bool finishArray()
//     {
//         m_state = State::Finished;
//         return true;
//     }

//     RcloneClient::ItemCallback m_onItem;
//     State m_state = State::BeforeArray;
//     QByteArray m_item;
//     int m_objectDepth = 0;
//     bool m_inString = false;
//     bool m_escaped = false;
//     bool m_allowArrayEnd = false;
// };
// } // namespace

// bool RcloneResult::success() const
// {
//     return started && !cancelled && !timedOut && exitCode == 0;
// }

// QString RcloneResult::errorMessage() const
// {
//     const QString stderrMessage = QString::fromUtf8(standardError).trimmed();
//     if (!stderrMessage.isEmpty()) {
//         return stderrMessage;
//     }

//     const QString stdoutMessage = QString::fromUtf8(standardOutput).trimmed();
//     if (!stdoutMessage.isEmpty()) {
//         return stdoutMessage;
//     }
//     if (!started) {
//         return QStringLiteral("rclone could not be started");
//     }
//     if (timedOut) {
//         return QStringLiteral("rclone timed out");
//     }
//     return QStringLiteral("rclone exited with code %1").arg(exitCode);
// }

// RcloneClient::RcloneClient(QString executable)
//     : m_executable(executable.isEmpty() ? locateExecutable() : std::move(executable))
// {
// }

// QString RcloneClient::executable() const
// {
//     return m_executable;
// }

// bool RcloneClient::isAvailable() const
// {
//     return !m_executable.isEmpty() && QFileInfo(m_executable).isExecutable();
// }

// RcloneResult RcloneClient::run(const QStringList &arguments, int timeoutMs, const CancellationCallback &isCancelled) const
// {
//     RcloneResult result;
//     if (!isAvailable()) {
//         return result;
//     }

//     QProcess process;
//     RcloneProcess::configureProcess(process, m_executable, arguments);
//     process.start();
//     result.started = process.waitForStarted(5000);
//     if (!result.started) {
//         result.standardError = process.errorString().toUtf8();
//         return result;
//     }

//     QElapsedTimer timer;
//     timer.start();
//     while (process.state() != QProcess::NotRunning) {
//         if (isCancelled && isCancelled()) {
//             result.cancelled = true;
//             RcloneProcess::stopProcess(process);
//             break;
//         }
//         if (timeoutMs >= 0 && timer.elapsed() >= timeoutMs) {
//             result.timedOut = true;
//             RcloneProcess::stopProcess(process);
//             break;
//         }
//         process.waitForFinished(100);
//     }

//     result.standardOutput = process.readAllStandardOutput();
//     result.standardError = process.readAllStandardError();
//     result.exitCode = process.exitCode();
//     return result;
// }

// QList<RcloneRemote> RcloneClient::remoteList(QString *error, const CancellationCallback &isCancelled) const
// {
//     const RcloneResult result = run({QStringLiteral("listremotes"), QStringLiteral("--json")}, 30000, isCancelled);
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return {};
//     }

//     return parseRemoteListWithTypes(result.standardOutput, error);
// }

// QStringList RcloneClient::remotes(QString *error, const CancellationCallback &isCancelled) const
// {
//     const QList<RcloneRemote> remoteListResult = remoteList(error, isCancelled);

//     QStringList result;
//     result.reserve(remoteListResult.size());
//     for (const RcloneRemote &remote : remoteListResult) {
//         result.append(remote.name);
//     }
//     return result;
// }

// QList<RcloneSharedDrive>
// RcloneClient::sharedDrives(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
// {
//     const RcloneResult result = run({QStringLiteral("backend"), QStringLiteral("drives"), remoteSpec}, 30000, isCancelled);
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return {};
//     }
//     return parseSharedDriveList(result.standardOutput, error);
// }

// std::optional<RcloneRemoteInfo> RcloneClient::remoteInfo(const QString &remote, QString *error, const CancellationCallback &isCancelled) const
// {
//     const RcloneResult result = run({QStringLiteral("config"), QStringLiteral("redacted"), remote}, 30000, isCancelled);
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return std::nullopt;
//     }
//     return parseRemoteInfo(result.standardOutput, error);
// }

// std::optional<bool>
// RcloneClient::mayHaveDuplicateNames(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
// {
//     const RcloneResult result = run({QStringLiteral("backend"), QStringLiteral("features"), remoteSpec, QStringLiteral("--json")}, 30000, isCancelled);
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return std::nullopt;
//     }
//     return parseDuplicateNameSupport(result.standardOutput, error);
// }

// bool RcloneClient::listStreaming(const QString &remoteSpec,
//                                   const ItemCallback &onItem,
//                                   QString *error,
//                                   const CancellationCallback &isCancelled,
//                                   RcloneListingDepth depth) const
// {
//     if (!isAvailable()) {
//         setError(error, QStringLiteral("rclone could not be started"));
//         return false;
//     }
//     if (!onItem) {
//         setError(error, QStringLiteral("No callback was provided for the rclone listing"));
//         return false;
//     }

//     RcloneResult result;
//     QProcess process;
//     QStringList arguments{QStringLiteral("lsjson"), remoteSpec};
//     if (depth == RcloneListingDepth::Recursive) {
//         arguments.append(QStringLiteral("--recursive"));
//     }
//     RcloneProcess::configureProcess(process, m_executable, arguments);
//     process.start();
//     result.started = process.waitForStarted(5000);
//     if (!result.started) {
//         result.standardError = process.errorString().toUtf8();
//         setError(error, result.errorMessage());
//         return false;
//     }

//     QString parsingError;
//     JsonArrayItemReader reader(onItem);
//     const auto consumeOutput = [&process, &result, &reader, &parsingError]() {
//         const QByteArray output = process.readAllStandardOutput();
//         RcloneProcess::appendLimited(result.standardError, process.readAllStandardError());
//         return output.isEmpty() || reader.feed(output, &parsingError);
//     };

//     QElapsedTimer timer;
//     timer.start();
//     bool parsingStopped = false;
//     while (process.state() != QProcess::NotRunning) {
//         if (isCancelled && isCancelled()) {
//             result.cancelled = true;
//             RcloneProcess::stopProcess(process);
//             break;
//         }
//         if (timer.elapsed() >= 120000) {
//             result.timedOut = true;
//             RcloneProcess::stopProcess(process);
//             break;
//         }

//         process.waitForReadyRead(100);
//         if (!consumeOutput()) {
//             parsingStopped = true;
//             RcloneProcess::stopProcess(process);
//             break;
//         }
//     }

//     if (!parsingStopped && !result.cancelled && !result.timedOut && !consumeOutput()) {
//         parsingStopped = true;
//     }
//     RcloneProcess::appendLimited(result.standardError, process.readAllStandardError());
//     result.exitCode = process.exitCode();

//     if (result.cancelled) {
//         return false;
//     }
//     if (result.timedOut) {
//         setError(error, result.errorMessage());
//         return false;
//     }
//     if (parsingStopped) {
//         setError(error, parsingError);
//         return false;
//     }
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return false;
//     }
//     return reader.finish(error);
// }

// QList<RcloneRemote> RcloneClient::parseRemoteListWithTypes(const QByteArray &json, QString *error)
// {
//     QJsonParseError parseError;
//     const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
//     if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
//         setError(error, parseError.errorString());
//         return {};
//     }

//     QList<RcloneRemote> remotes;
//     for (const QJsonValue &value : document.array()) {
//         RcloneRemote remote;
//         if (value.isString()) {
//             remote.name = value.toString();
//         } else if (value.isObject()) {
//             const QJsonObject object = value.toObject();
//             remote.name = object.value(QStringLiteral("name")).toString();
//             remote.type = object.value(QStringLiteral("type")).toString();
//         }
//         if (remote.name.endsWith(QLatin1Char(':'))) {
//             remote.name.chop(1);
//         }
//         if (!remote.name.isEmpty()) {
//             remotes.append(std::move(remote));
//         }
//     }
//     std::sort(remotes.begin(), remotes.end(), [](const RcloneRemote &left, const RcloneRemote &right) {
//         return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
//     });
//     return remotes;
// }

// QStringList RcloneClient::parseRemoteList(const QByteArray &json, QString *error)
// {
//     const QList<RcloneRemote> parsedRemotes = parseRemoteListWithTypes(json, error);
//     QStringList remotes;
//     remotes.reserve(parsedRemotes.size());
//     for (const RcloneRemote &remote : parsedRemotes) {
//         remotes.append(remote.name);
//     }
//     return remotes;
// }

// QList<RcloneSharedDrive> RcloneClient::parseSharedDriveList(const QByteArray &json, QString *error)
// {
//     QJsonParseError parseError;
//     const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
//     if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
//         setError(error, parseError.errorString());
//         return {};
//     }

//     QList<RcloneSharedDrive> drives;
//     for (const QJsonValue &value : document.array()) {
//         if (!value.isObject()) {
//             setError(error, QStringLiteral("rclone returned an invalid Shared Drive entry"));
//             return {};
//         }

//         const QJsonObject object = value.toObject();
//         RcloneSharedDrive drive;
//         drive.id = object.value(QStringLiteral("id")).toString();
//         drive.name = object.value(QStringLiteral("name")).toString();
//         if (drive.id.isEmpty() || drive.name.isEmpty()) {
//             setError(error, QStringLiteral("rclone returned an incomplete Shared Drive entry"));
//             return {};
//         }
//         drives.append(std::move(drive));
//     }
//     std::sort(drives.begin(), drives.end(), [](const RcloneSharedDrive &left, const RcloneSharedDrive &right) {
//         const int comparison = QString::compare(left.name, right.name, Qt::CaseInsensitive);
//         return comparison == 0 ? left.id < right.id : comparison < 0;
//     });
//     return drives;
// }

// std::optional<bool> RcloneClient::parseDuplicateNameSupport(const QByteArray &json, QString *error)
// {
//     QJsonParseError parseError;
//     const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
//     if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
//         setError(error, parseError.errorString());
//         return std::nullopt;
//     }

//     const QJsonObject features = document.object().value(QStringLiteral("Features")).toObject();
//     const QJsonValue duplicateFiles = features.value(QStringLiteral("DuplicateFiles"));
//     const QJsonValue mergeDirectories = features.value(QStringLiteral("MergeDirs"));
//     if ((!duplicateFiles.isBool() && !mergeDirectories.isBool()) || features.isEmpty()) {
//         setError(error, QStringLiteral("rclone did not report duplicate-name support for this remote"));
//         return std::nullopt;
//     }

//     // MergeDirs is a second signal for duplicate directories.  Treat either
//     // capability as requiring the conservative path, including wrapped
//     // remotes such as crypt over Google Drive.
//     return duplicateFiles.toBool() || mergeDirectories.toBool();
// }

// std::optional<RcloneRemoteInfo> RcloneClient::parseRemoteInfo(const QByteArray &config, QString *error)
// {
//     RcloneRemoteInfo info;
//     bool foundSection = false;

//     const QList<QByteArray> lines = config.split('\n');
//     for (const QByteArray &rawLine : lines) {
//         const QString line = QString::fromUtf8(rawLine).trimmed();
//         if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
//             info.name = line.mid(1, line.size() - 2).trimmed();
//             foundSection = !info.name.isEmpty();
//             continue;
//         }
//         if (!foundSection) {
//             continue;
//         }

//         const qsizetype separator = line.indexOf(QLatin1Char('='));
//         if (separator < 0) {
//             continue;
//         }
//         const QString key = line.left(separator).trimmed();
//         const QString value = line.mid(separator + 1).trimmed();
//         if (key == QLatin1String("type")) {
//             info.type = value;
//         } else if (key == QLatin1String("client_id")) {
//             info.hasClientId = !value.isEmpty();
//         } else if (key == QLatin1String("root_folder_id")) {
//             info.hasRootFolderId = !value.isEmpty();
//         }
//     }

//     if (!foundSection) {
//         setError(error, QStringLiteral("No remote section found in rclone configuration"));
//         return std::nullopt;
//     }
//     return info;
// }

// QList<RcloneItem> RcloneClient::list(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
// {
//     QList<RcloneItem> items;
//     if (!listStreaming(remoteSpec,
//                        [&items](const RcloneItem &item) {
//                            items.append(item);
//                            return true;
//                        },
//                        error,
//                        isCancelled)) {
//         return {};
//     }
//     return items;
// }

// std::optional<RcloneItem> RcloneClient::stat(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
// {
//     const RcloneResult result = run({QStringLiteral("lsjson"), remoteSpec, QStringLiteral("--stat")}, 60000, isCancelled);
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return std::nullopt;
//     }
//     return parseItem(result.standardOutput, error);
// }

// std::optional<RcloneSpace> RcloneClient::about(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
// {
//     const RcloneResult result = run({QStringLiteral("about"), remoteSpec, QStringLiteral("--json")}, 60000, isCancelled);
//     if (!result.success()) {
//         setError(error, result.errorMessage());
//         return std::nullopt;
//     }

//     QJsonParseError parseError;
//     const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);
//     if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
//         setError(error, parseError.errorString());
//         return std::nullopt;
//     }

//     const QJsonObject object = document.object();
//     RcloneSpace space;
//     if (object.contains(QStringLiteral("total"))) {
//         space.total = object.value(QStringLiteral("total")).toVariant().toLongLong();
//     }
//     if (object.contains(QStringLiteral("free"))) {
//         space.free = object.value(QStringLiteral("free")).toVariant().toLongLong();
//     }
//     if (object.contains(QStringLiteral("used"))) {
//         space.used = object.value(QStringLiteral("used")).toVariant().toLongLong();
//     }
//     return space;
// }

// QString RcloneClient::locateExecutable()
// {
//     const QString overridden = qEnvironmentVariable("KIO_RCLONE_EXECUTABLE");
//     if (!overridden.isEmpty() && QFileInfo(overridden).isExecutable()) {
//         return overridden;
//     }

//     const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("rclone"));
//     if (!fromPath.isEmpty()) {
//         return fromPath;
//     }

//     const QString userLocal = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.local/bin/rclone");
//     return QFileInfo(userLocal).isExecutable() ? userLocal : QString();
// }

// QList<RcloneItem> RcloneClient::parseItemList(const QByteArray &json, QString *error)
// {
//     QJsonParseError parseError;
//     const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
//     if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
//         setError(error, parseError.errorString());
//         return {};
//     }

//     QList<RcloneItem> items;
//     const QJsonArray array = document.array();
//     items.reserve(array.size());
//     for (const QJsonValue &value : array) {
//         if (const auto item = itemFromObject(value.toObject())) {
//             items.append(*item);
//         }
//     }
//     return items;
// }

// std::optional<RcloneItem> RcloneClient::parseItem(const QByteArray &json, QString *error)
// {
//     QJsonParseError parseError;
//     const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
//     if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
//         setError(error, parseError.errorString());
//         return std::nullopt;
//     }
//     return itemFromObject(document.object());
// }

// bool RcloneClient::isNotFoundError(const QString &error)
// {
//     const QString lowered = error.toLower();
//     return lowered.contains(QStringLiteral("not found")) || lowered.contains(QStringLiteral("doesn't exist"))
//         || lowered.contains(QStringLiteral("does not exist")) || lowered.contains(QStringLiteral("directory not found"))
//         || lowered.contains(QStringLiteral("object not found"));
// }
