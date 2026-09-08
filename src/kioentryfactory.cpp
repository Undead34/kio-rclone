/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kioentryfactory.h"

#include "rcloneentryformat.h"
#include "rcloneurl.h"

#include <KLocalizedString>

#include <sys/stat.h>

namespace KioEntryFactory
{
KIO::UDSEntry root()
{
    KIO::UDSEntry entry;
    entry.reserve(6);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Rclone Remotes"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-kio-rclone"));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry configure()
{
    KIO::UDSEntry entry;
    entry.reserve(8);

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, RcloneUrl::ConfigureEntry);

    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Configure Remotes…"));

    // Not a directory: it cannot be entered or listed.
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);

    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH);

    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("configure"));

    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, RcloneUrl::ConfigurationLauncherMimeType);

    entry.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, RcloneUrl::configurationLauncherUrl().toString());

    entry.fastInsert(KIO::UDSEntry::UDS_HIDDEN, 0);

    return entry;
}

KIO::UDSEntry remote(const QString &name, bool currentDirectory, const QString &type)
{
    KIO::UDSEntry entry;
    entry.reserve(6);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, currentDirectory ? QStringLiteral(".") : name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, currentDirectory ? name : name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, RcloneEntryFormat::iconForRemoteType(type));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry item(const RcloneItem &item)
{
    KIO::UDSEntry entry;
    entry.reserve(7);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, item.name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, item.isDirectory ? S_IFDIR : S_IFREG);
    const mode_t fileAccess = item.readOnly ? S_IRUSR | S_IRGRP | S_IROTH : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, item.isDirectory ? S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH : fileAccess);
    if (item.size >= 0) {
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, item.size);
    }
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, RcloneEntryFormat::fallbackMimeType(item));
    if (item.ambiguous) {
        entry.fastInsert(KIO::UDSEntry::UDS_COMMENT, i18n("Multiple remote objects have this name. KIO Rclone is showing the newest one."));
    }
    if (item.isDirectory) {
        entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder"));
    }
    if (item.modificationTime.isValid()) {
        entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, item.modificationTime.toSecsSinceEpoch());
    }
    return entry;
}
}
