/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clienthelpers.h"

#include <cmath>
#include <optional>

#include <QJsonValue>
#include <QUuid>

namespace RcloneClientHelpers
{

static RcloneStatus clientSuccess()
{
    return {
        true,
        {RcloneErrorCode::None, {}, 0},
    };
}

static RcloneStatus clientFailure(RcloneErrorCode code,
                                  const QString &message)
{
    return {
        false,
        {code, message, -1},
    };
}

QString uploadStagingSpec(const QString &remoteDest)
{
    const qsizetype nameStart =
        remoteDest.lastIndexOf(QLatin1Char('/')) + 1;
    const QString fileName = remoteDest.mid(nameStart);
    const qsizetype extensionStart =
        fileName.lastIndexOf(QLatin1Char('.'));
    const QString extension = extensionStart > 0
            && fileName.size() - extensionStart <= 32
        ? fileName.mid(extensionStart)
        : QString();
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stagingName =
        QStringLiteral(".kio-rclone-upload-%1%2")
            .arg(token, extension);

    return remoteDest.left(nameStart) + stagingName;
}

RcloneStatus targetStillMatches(
    const RcloneTargetSnapshot &expected,
    const RcloneResponse<RcloneItem> &current)
{
    if (!current.success()) {
        if (current.error.code != RcloneErrorCode::NotFound) {
            return {false, current.error};
        }

        if (!expected.exists) {
            return clientSuccess();
        }

        return clientFailure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral(
                "The remote file was deleted while local changes were being prepared"));
    }

    if (!expected.exists) {
        return clientFailure(
            RcloneErrorCode::AlreadyExists,
            QStringLiteral(
                "A remote file was created while local changes were being prepared"));
    }

    if (current.data->isDirectory != expected.isDirectory) {
        return clientFailure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral(
                "The remote destination changed type while local changes were being prepared"));
    }

    if ((!expected.id.isEmpty()
         && !current.data->id.isEmpty()
         && expected.id != current.data->id)
        || (expected.modificationTime.isValid()
            && current.data->modificationTime.isValid()
            && expected.modificationTime
                != current.data->modificationTime)
        || (expected.size >= 0
            && current.data->size >= 0
            && expected.size != current.data->size)) {
        return clientFailure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral(
                "The remote file changed while local changes were being prepared"));
    }

    return clientSuccess();
}

RcloneErrorCode classifyProcessFailure(
    int exitCode,
    const QString &diagnostic)
{
    // rclone documents these as the two not-found exit statuses. They are
    // stable across backends and do not depend on a localized error string.
    if (exitCode == 3 || exitCode == 4) {
        return RcloneErrorCode::NotFound;
    }

    const QString normalized = diagnostic.trimmed().toCaseFolded();

    if (normalized.contains(QStringLiteral("permission denied"))
        || normalized.contains(QStringLiteral("access denied"))
        || normalized.contains(QStringLiteral("unauthorized"))
        || normalized.contains(QStringLiteral("forbidden"))) {
        return RcloneErrorCode::PermissionDenied;
    }

    if (normalized.contains(QStringLiteral("already exists"))
        || normalized.contains(QStringLiteral("alreadyexists"))) {
        return RcloneErrorCode::AlreadyExists;
    }

    return RcloneErrorCode::ProcessFailure;
}

static std::optional<qint64> jsonNonNegativeInteger(
    const QJsonObject &object,
    const QString &key)
{
    const QJsonValue value = object.value(key);

    if (!value.isDouble()) {
        return std::nullopt;
    }

    const qint64 number = value.toInteger(-1);

    if (number < 0) {
        return std::nullopt;
    }

    return number;
}

static std::optional<qint64> jsonNonNegativeNumber(
    const QJsonObject &object,
    const QString &key)
{
    const QJsonValue value = object.value(key);

    if (!value.isDouble()) {
        return std::nullopt;
    }

    const double number = value.toDouble();

    if (!std::isfinite(number)
        || number < 0
        || number >= 0x1p63) {
        return std::nullopt;
    }

    return static_cast<qint64>(number);
}

void updateTransferStats(const QJsonObject &stats,
                         qint64 fileSize,
                         RcloneClient::TransferStats &progress)
{
    if (const auto bytes =
            jsonNonNegativeInteger(stats, QStringLiteral("bytes"))) {
        // El tamaño local es el total lógico del archivo.
        progress.bytes = fileSize >= 0
            ? qMin(*bytes, fileSize)
            : *bytes;
    }

    if (const auto speed =
            jsonNonNegativeNumber(stats, QStringLiteral("speed"))) {
        progress.speed = *speed;
    }

    if (const auto eta =
            jsonNonNegativeNumber(stats, QStringLiteral("eta"))) {
        progress.etaSeconds = *eta;
    }

    if (fileSize > 0) {
        progress.percentage = qBound(
            0,
            static_cast<int>(
                100.0 * progress.bytes / fileSize),
            100);
    }
}

} // namespace RcloneClientHelpers
