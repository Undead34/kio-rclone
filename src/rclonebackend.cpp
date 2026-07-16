/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rclonebackend.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace
{
std::optional<RcloneItem> itemFromObject(const QJsonObject &object)
{
    if (object.isEmpty()) {
        return std::nullopt;
    }

    RcloneItem item;
    item.name = object.value(QStringLiteral("Name")).toString();
    item.path = object.value(QStringLiteral("Path")).toString();
    item.id = object.value(QStringLiteral("ID")).toString();
    item.mimeType = object.value(QStringLiteral("MimeType")).toString();
    item.size = object.value(QStringLiteral("Size")).toVariant().toLongLong();
    item.isDirectory = object.value(QStringLiteral("IsDir")).toBool();
    item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODateWithMs);
    if (!item.modificationTime.isValid()) {
        item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODate);
    }

    if (item.name.isEmpty() && !item.path.isEmpty()) {
        item.name = item.path.section(QLatin1Char('/'), -1);
    }
    return item;
}

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}
} // namespace

bool RcloneResult::success() const
{
    return started && !cancelled && !timedOut && exitCode == 0;
}

QString RcloneResult::errorMessage() const
{
    const QString stderrMessage = QString::fromUtf8(standardError).trimmed();
    if (!stderrMessage.isEmpty()) {
        return stderrMessage;
    }

    const QString stdoutMessage = QString::fromUtf8(standardOutput).trimmed();
    if (!stdoutMessage.isEmpty()) {
        return stdoutMessage;
    }
    if (!started) {
        return QStringLiteral("rclone could not be started");
    }
    if (timedOut) {
        return QStringLiteral("rclone timed out");
    }
    return QStringLiteral("rclone exited with code %1").arg(exitCode);
}

RcloneBackend::RcloneBackend(QString executable)
    : m_executable(executable.isEmpty() ? locateExecutable() : std::move(executable))
{
}

QString RcloneBackend::executable() const
{
    return m_executable;
}

bool RcloneBackend::isAvailable() const
{
    return !m_executable.isEmpty() && QFileInfo(m_executable).isExecutable();
}

RcloneResult RcloneBackend::run(const QStringList &arguments, int timeoutMs, const CancellationCallback &isCancelled) const
{
    RcloneResult result;
    if (!isAvailable()) {
        return result;
    }

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProgram(m_executable);
    process.setArguments(arguments);
    process.start();
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            process.terminate();
            if (!process.waitForFinished(1000)) {
                process.kill();
                process.waitForFinished(1000);
            }
            break;
        }
        if (timeoutMs >= 0 && timer.elapsed() >= timeoutMs) {
            result.timedOut = true;
            process.kill();
            process.waitForFinished(1000);
            break;
        }
        process.waitForFinished(100);
    }

    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    result.exitCode = process.exitCode();
    return result;
}

QStringList RcloneBackend::remotes(QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("listremotes"), QStringLiteral("--json")}, 30000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return {};
    }

    return parseRemoteList(result.standardOutput, error);
}

QStringList RcloneBackend::parseRemoteList(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(error, parseError.errorString());
        return {};
    }

    QStringList remotes;
    for (const QJsonValue &value : document.array()) {
        QString name;
        if (value.isString()) {
            name = value.toString();
        } else if (value.isObject()) {
            name = value.toObject().value(QStringLiteral("name")).toString();
        }
        if (name.endsWith(QLatin1Char(':'))) {
            name.chop(1);
        }
        if (!name.isEmpty()) {
            remotes.append(name);
        }
    }
    remotes.sort(Qt::CaseInsensitive);
    return remotes;
}

QList<RcloneItem> RcloneBackend::list(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("lsjson"), remoteSpec}, 120000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return {};
    }
    return parseItemList(result.standardOutput, error);
}

std::optional<RcloneItem> RcloneBackend::stat(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("lsjson"), remoteSpec, QStringLiteral("--stat")}, 60000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return std::nullopt;
    }
    return parseItem(result.standardOutput, error);
}

std::optional<RcloneSpace> RcloneBackend::about(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("about"), remoteSpec, QStringLiteral("--json")}, 60000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    RcloneSpace space;
    if (object.contains(QStringLiteral("total"))) {
        space.total = object.value(QStringLiteral("total")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("free"))) {
        space.free = object.value(QStringLiteral("free")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("used"))) {
        space.used = object.value(QStringLiteral("used")).toVariant().toLongLong();
    }
    return space;
}

QString RcloneBackend::locateExecutable()
{
    const QString overridden = qEnvironmentVariable("KIO_RCLONE_EXECUTABLE");
    if (!overridden.isEmpty() && QFileInfo(overridden).isExecutable()) {
        return overridden;
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("rclone"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QString userLocal = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.local/bin/rclone");
    return QFileInfo(userLocal).isExecutable() ? userLocal : QString();
}

QList<RcloneItem> RcloneBackend::parseItemList(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(error, parseError.errorString());
        return {};
    }

    QList<RcloneItem> items;
    const QJsonArray array = document.array();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (const auto item = itemFromObject(value.toObject())) {
            items.append(*item);
        }
    }
    return items;
}

std::optional<RcloneItem> RcloneBackend::parseItem(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }
    return itemFromObject(document.object());
}

bool RcloneBackend::isNotFoundError(const QString &error)
{
    const QString lowered = error.toLower();
    return lowered.contains(QStringLiteral("not found")) || lowered.contains(QStringLiteral("doesn't exist"))
        || lowered.contains(QStringLiteral("does not exist")) || lowered.contains(QStringLiteral("directory not found"))
        || lowered.contains(QStringLiteral("object not found"));
}
