/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "models.h"

#include <cmath>
#include <limits>

#include <QJsonValue>

namespace {

bool readSpaceMetric(const QJsonObject &object,
                     const QString &key,
                     qint64 &destination)
{
    const QJsonValue value = object.value(key);

    if (value.isUndefined() || value.isNull()) {
        return true;
    }

    if (!value.isDouble()) {
        return false;
    }

    const double number = value.toDouble();

    if (!std::isfinite(number)
        || number < 0
        || std::floor(number) != number
        || number >= 0x1p63) {
        return false;
    }

    destination = static_cast<qint64>(number);
    return true;
}


bool readOptionalString(const QJsonObject &object,
                        const QString &key,
                        QString &destination)
{
    const QJsonValue value = object.value(key);

    if (value.isUndefined() || value.isNull()) {
        return true;
    }

    if (!value.isString()) {
        return false;
    }

    destination = value.toString();
    return true;
}

bool readOptionalBool(const QJsonObject &object,
                      const QString &key,
                      bool &destination)
{
    const QJsonValue value = object.value(key);

    if (value.isUndefined() || value.isNull()) {
        return true;
    }

    if (!value.isBool()) {
        return false;
    }

    destination = value.toBool();
    return true;
}

std::optional<QDateTime> parseRcloneDateTime(QString value)
{
    if (value.isEmpty()) {
        return QDateTime{};
    }

    const qsizetype timeSeparator = value.indexOf(QLatin1Char('T'));
    const qsizetype decimalPoint =
        value.indexOf(QLatin1Char('.'), timeSeparator);

    if (decimalPoint >= 0) {
        qsizetype fractionEnd = decimalPoint + 1;

        while (fractionEnd < value.size()
               && value.at(fractionEnd).isDigit()) {
            ++fractionEnd;
        }

        QString fraction =
            value.mid(decimalPoint + 1,
                      fractionEnd - decimalPoint - 1);

        // QDateTime conserva milisegundos. rclone puede producir
        // fracciones con precisión superior.
        fraction = fraction.left(3);

        while (fraction.size() < 3) {
            fraction.append(QLatin1Char('0'));
        }

        value.replace(
            decimalPoint + 1,
            fractionEnd - decimalPoint - 1,
            fraction);

        const QDateTime dateTime =
            QDateTime::fromString(value, Qt::ISODateWithMs);

        if (!dateTime.isValid()) {
            return std::nullopt;
        }

        return dateTime;
    }

    const QDateTime dateTime =
        QDateTime::fromString(value, Qt::ISODate);

    if (!dateTime.isValid()) {
        return std::nullopt;
    }

    return dateTime;
}

} // namespace


std::optional<RcloneItem>
RcloneItem::fromJson(const QJsonObject &object)
{
    const QJsonValue pathValue =
        object.value(QStringLiteral("Path"));

    const QJsonValue nameValue =
        object.value(QStringLiteral("Name"));

    const QJsonValue sizeValue =
        object.value(QStringLiteral("Size"));

    const QJsonValue isDirValue =
        object.value(QStringLiteral("IsDir"));

    if (!pathValue.isString()
        || !nameValue.isString()
        || !sizeValue.isDouble()
        || !isDirValue.isBool()) {
        return std::nullopt;
    }

    constexpr qint64 invalidSize =
        std::numeric_limits<qint64>::min();

    const qint64 size = sizeValue.toInteger(invalidSize);

    if (size == invalidSize || size < -1) {
        return std::nullopt;
    }

    RcloneItem item;

    item.path = pathValue.toString();
    item.name = nameValue.toString();
    item.size = size;
    item.isDirectory = isDirValue.toBool();

    if (!readOptionalString(
            object,
            QStringLiteral("ID"),
            item.id)
        || !readOptionalString(
            object,
            QStringLiteral("OrigID"),
            item.originalId)
        || !readOptionalString(
            object,
            QStringLiteral("MimeType"),
            item.mimeType)
        || !readOptionalString(
            object,
            QStringLiteral("Tier"),
            item.tier)
        || !readOptionalBool(
            object,
            QStringLiteral("IsBucket"),
            item.isBucket)) {
        return std::nullopt;
    }

    const QJsonValue modTimeValue =
        object.value(QStringLiteral("ModTime"));

    if (!modTimeValue.isUndefined()
        && !modTimeValue.isNull()) {
        if (!modTimeValue.isString()) {
            return std::nullopt;
        }

        const auto modificationTime =
            parseRcloneDateTime(modTimeValue.toString());

        if (!modificationTime) {
            return std::nullopt;
        }

        item.modificationTime = *modificationTime;
    }

    return item;
}

std::optional<RcloneSpace>
RcloneSpace::fromJson(const QJsonObject &object)
{
    RcloneSpace space;

    if (!readSpaceMetric(object, QStringLiteral("total"), space.total)
        || !readSpaceMetric(object, QStringLiteral("free"), space.free)
        || !readSpaceMetric(object, QStringLiteral("used"), space.used)
        || !readSpaceMetric(object, QStringLiteral("trashed"), space.trashed)
        || !readSpaceMetric(object, QStringLiteral("other"), space.other)) {
        return std::nullopt;
    }

    return space;
}

std::optional<RcloneRemote>
RcloneRemote::fromJson(const QJsonObject &object)
{
    const QJsonValue name = object.value(QStringLiteral("name"));
    const QJsonValue type = object.value(QStringLiteral("type"));

    if (!name.isString() || !type.isString()) {
        return std::nullopt;
    }

    RcloneRemote remote;
    remote.name = name.toString();
    remote.type = type.toString();

    if (remote.name.isEmpty() || remote.type.isEmpty()) {
        return std::nullopt;
    }

    return remote;
}


std::optional<RcloneSharedDrive>
RcloneSharedDrive::fromJson(const QJsonObject &object)
{
    QJsonValue id = object.value(QStringLiteral("id"));
    QJsonValue name = object.value(QStringLiteral("name"));

    if (!id.isString()) {
        id = object.value(QStringLiteral("ID"));
    }

    if (!name.isString()) {
        name = object.value(QStringLiteral("Name"));
    }

    if (!id.isString() || !name.isString()) {
        return std::nullopt;
    }

    RcloneSharedDrive drive;
    drive.id = id.toString();
    drive.name = name.toString();

    if (drive.id.isEmpty() || drive.name.isEmpty()) {
        return std::nullopt;
    }

    return drive;
}
