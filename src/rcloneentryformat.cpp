/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneentryformat.h"

#include <QHash>
#include <QLatin1Char>
#include <QMimeDatabase>

namespace RcloneEntryFormat
{
QString iconForRemoteType(const QString &type)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("drive"), QStringLiteral("folder-gdrive")},
        {QStringLiteral("dropbox"), QStringLiteral("folder-dropbox")},
        {QStringLiteral("onedrive"), QStringLiteral("folder-onedrive")},
    };
    return icons.value(type, QStringLiteral("folder-cloud"));
}

QString fallbackMimeType(const RcloneItem &item)
{
    if (item.isDirectory) {
        return QStringLiteral("inode/directory");
    }
    if (!item.mimeType.isEmpty()) {
        return item.mimeType;
    }
    return QMimeDatabase().mimeTypeForFile(item.name, QMimeDatabase::MatchExtension).name();
}

QString itemVersion(const RcloneItem &item)
{
    return item.id + QLatin1Char('\n') + item.modificationTime.toUTC().toString(Qt::ISODateWithMs) + QLatin1Char('\n') + QString::number(item.size);
}

bool preferItem(const RcloneItem &candidate, const RcloneItem &current)
{
    if (candidate.modificationTime.isValid() != current.modificationTime.isValid()) {
        return candidate.modificationTime.isValid();
    }
    if (candidate.modificationTime != current.modificationTime) {
        return candidate.modificationTime > current.modificationTime;
    }
    if (!candidate.id.isEmpty() && !current.id.isEmpty() && candidate.id != current.id) {
        return candidate.id < current.id;
    }
    return false;
}
}
