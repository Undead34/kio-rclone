/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "navigationentry.h"

RcloneNavigationEntry
RcloneNavigationEntry::virtualDirectory(const QString &name,
                                        const QUrl &url,
                                        const QString &iconName)
{
    RcloneNavigationEntry entry;
    entry.name = name;
    entry.url = url;
    entry.iconName = iconName;
    entry.mimeType = QStringLiteral("inode/directory");
    entry.isDirectory = true;
    entry.readOnly = true;

    return entry;
}

RcloneNavigationEntry
RcloneNavigationEntry::virtualFile(const QString &name,
                                   const QUrl &targetUrl,
                                   const QString &mimeType,
                                   const QString &iconName,
                                   const QString &displayName)
{
    RcloneNavigationEntry entry;
    entry.name = name;
    entry.displayName = displayName;
    entry.targetUrl = targetUrl;
    entry.mimeType = mimeType;
    entry.iconName = iconName;
    entry.isDirectory = false;
    entry.readOnly = true;
    entry.hidden = false;

    return entry;
}

RcloneNavigationEntry
RcloneNavigationEntry::fromItem(const RcloneItem &item,
                                const QUrl &url)
{
    RcloneNavigationEntry entry;

    entry.name = item.name.isEmpty()
        ? item.path.section(QLatin1Char('/'), -1)
        : item.name;

    entry.displayName = entry.name;
    entry.url = url;

    entry.id = item.id;
    entry.originalId = item.originalId;

    entry.mimeType = item.isDirectory
        ? QStringLiteral("inode/directory")
        : item.mimeType;

    entry.size = item.size;
    entry.isDirectory = item.isDirectory;
    entry.readOnly = item.readOnly;
    entry.modificationTime = item.modificationTime;

    return entry;
}
