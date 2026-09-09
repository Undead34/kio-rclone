/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "entrybuilder.h"

#include "rclone/entryformat.h"
#include "rclone/url.h"

#include <KLocalizedString>
#include <kio_version.h>

#include <sys/stat.h>

namespace
{
void reserveEntry(KIO::UDSEntry &entry, int strings, int numbers)
{
#if KIO_VERSION >= QT_VERSION_CHECK(6, 29, 0)
    entry.reserveStrings(strings);
    entry.reserveNumbers(numbers);
#else
    entry.reserve(strings + numbers);
#endif
}
}

namespace KioEntryBuilder
{
KIO::UDSEntry root()
{
    KIO::UDSEntry entry;
    reserveEntry(entry, 4, 2);
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
    reserveEntry(entry, 5, 3);

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
    reserveEntry(entry, 4, 2);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, currentDirectory ? QStringLiteral(".") : name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, currentDirectory ? name : name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, RcloneEntryFormat::iconForRemoteType(type));
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

KIO::UDSEntry directory(const QString &name,
                         const QString &displayName,
                         const QString &iconName,
                         bool writable)
{
    KIO::UDSEntry entry;
    reserveEntry(entry, 4, 2);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, displayName);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    const mode_t access = writable ? S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
                                   : S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, access);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, iconName);
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    return entry;
}

bool isRepresentable(const RcloneItem &item)
{
    // UDS_NAME becomes a URL path component. KIO reserves dot components, and
    // neither literal slashes nor NUL are representable in one component.
    // rclone can expose such names on some cloud providers.
    return !item.name.isEmpty() && !item.name.contains(QLatin1Char('/')) && item.name != QLatin1String(".")
        && item.name != QLatin1String("..") && !item.name.contains(QChar::Null)
        && (item.path.isEmpty() || item.path == item.name);
}

KIO::UDSEntry item(const RcloneItem &item)
{
    KIO::UDSEntry entry;
    reserveEntry(entry, 4, 4);
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, item.name);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, item.isDirectory ? S_IFDIR : S_IFREG);
    const mode_t fileAccess = item.readOnly ? S_IRUSR | S_IRGRP | S_IROTH : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    const mode_t directoryAccess = item.readOnly ? S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
                                                  : S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, item.isDirectory ? directoryAccess : fileAccess);
    if (item.size >= 0) {
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, item.size);
    }
    entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, RcloneEntryFormat::fallbackMimeType(item));
    if (item.ambiguous) {
        entry.fastInsert(KIO::UDSEntry::UDS_COMMENT,
                         item.isDirectory
                             ? i18n("Multiple remote directories have this name. This ambiguous folder is read-only; "
                                    "resolve it with rclone dedupe before changing it.")
                             : i18n("Multiple remote objects have this name. KIO Rclone selected one read-only."));
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
