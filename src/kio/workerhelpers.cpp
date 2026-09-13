/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "workerhelpers.h"

#include <KLocalizedString>

#include <QHash>
#include <QUrlQuery>

#include <sys/stat.h>

namespace RcloneWorkerHelpers
{

KIO::WorkerResult malformedUrlResult(const QUrl &url)
{
    return KIO::WorkerResult::fail(
        KIO::ERR_MALFORMED_URL,
        url.toDisplayString());
}

KIO::WorkerResult rcloneFailure(const RcloneError &error,
                                const QUrl &url,
                                int fallbackError)
{
    const QString display = url.toDisplayString();

    switch (error.code) {
    case RcloneErrorCode::Cancelled:
    case RcloneErrorCode::Aborted:
        return KIO::WorkerResult::fail(
            KIO::ERR_USER_CANCELED,
            display);

    case RcloneErrorCode::NotFound:
        return KIO::WorkerResult::fail(
            KIO::ERR_DOES_NOT_EXIST,
            display);

    case RcloneErrorCode::PermissionDenied:
        return KIO::WorkerResult::fail(
            KIO::ERR_ACCESS_DENIED,
            display);

    case RcloneErrorCode::Unsupported:
        return KIO::WorkerResult::fail(
            KIO::ERR_UNSUPPORTED_ACTION,
            error.message);

    case RcloneErrorCode::TimedOut:
    case RcloneErrorCode::InvalidResponse:
    case RcloneErrorCode::ProcessFailure:
    case RcloneErrorCode::Unknown:
    case RcloneErrorCode::AlreadyExists:
    case RcloneErrorCode::None:
        break;
    }

    if (!error.message.isEmpty()) {
        return KIO::WorkerResult::fail(
            fallbackError,
            error.message);
    }

    return KIO::WorkerResult::fail(
        fallbackError,
        display);
}

KIO::WorkerResult listFailure(const RcloneError &error,
                              const QUrl &url)
{
    return rcloneFailure(
        error,
        url,
        KIO::ERR_CANNOT_ENTER_DIRECTORY);
}

KIO::WorkerResult statFailure(const RcloneError &error,
                              const QUrl &url)
{
    return rcloneFailure(
        error,
        url,
        KIO::ERR_CANNOT_STAT);
}

QString iconForRemoteType(const QString &type)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("drive"), QStringLiteral("folder-gdrive")},
        {QStringLiteral("dropbox"), QStringLiteral("folder-dropbox")},
        {QStringLiteral("onedrive"), QStringLiteral("folder-onedrive")},
    };
    return icons.value(type, QStringLiteral("folder-cloud"));
}

KIO::UDSEntry makeEntry(const RcloneNavigationEntry &entry)
{
    KIO::UDSEntry uds;

    uds.fastInsert(KIO::UDSEntry::UDS_NAME, entry.name);

    if (!entry.displayName.isEmpty()) {
        uds.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, entry.displayName);
    }

    uds.fastInsert(
        KIO::UDSEntry::UDS_FILE_TYPE,
        entry.isDirectory ? S_IFDIR : S_IFREG);

    uds.fastInsert(
        KIO::UDSEntry::UDS_ACCESS,
        entry.readOnly
            ? (entry.isDirectory ? 0500 : 0400)
            : (entry.isDirectory ? 0700 : 0600));

    if (!entry.mimeType.isEmpty()) {
        uds.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, entry.mimeType);
    }

    if (!entry.iconName.isEmpty()) {
        uds.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, entry.iconName);
    }

    if (entry.size >= 0) {
        uds.fastInsert(KIO::UDSEntry::UDS_SIZE, entry.size);
    }

    if (entry.modificationTime.isValid()) {
        uds.fastInsert(
            KIO::UDSEntry::UDS_MODIFICATION_TIME,
            entry.modificationTime.toSecsSinceEpoch());
    }

    if (entry.hidden.has_value()) {
        uds.fastInsert(KIO::UDSEntry::UDS_HIDDEN, *entry.hidden);
    }

    if (entry.url.isValid()) {
        uds.fastInsert(
            KIO::UDSEntry::UDS_URL,
            entry.url.toString());
    }

    if (entry.targetUrl.isValid()) {
        uds.fastInsert(
            KIO::UDSEntry::UDS_TARGET_URL,
            entry.targetUrl.toString());
    }

    return uds;
}

QUrl withItemIdentity(QUrl url,
                      const RcloneItem &item)
{
    QUrlQuery query(url);

    if (!item.id.isEmpty()) {
        query.removeAllQueryItems(QStringLiteral("rclone-id"));
        query.addQueryItem(QStringLiteral("rclone-id"), item.id);
    }

    if (!item.originalId.isEmpty()) {
        query.removeAllQueryItems(QStringLiteral("rclone-orig-id"));
        query.addQueryItem(QStringLiteral("rclone-orig-id"), item.originalId);
    }

    url.setQuery(query);
    return url;
}

QUrl withRemoteType(QUrl url,
                    const QString &remoteType)
{
    QUrlQuery query(url);
    query.removeAllQueryItems(QStringLiteral("rclone-remote-type"));
    query.addQueryItem(QStringLiteral("rclone-remote-type"), remoteType);
    url.setQuery(query);
    return url;
}

RcloneNavigationEntry virtualDirectoryForStat(
    const RcloneLocation &location,
    const QUrl &url)
{
    QString name;
    QString iconName = QStringLiteral("folder");
    bool readOnly = true;

    switch (location.kind()) {
    case RcloneLocation::Kind::Root:
        name = QStringLiteral(".");
        break;

    case RcloneLocation::Kind::Standard:
        name = location.remoteName();
        iconName = iconForRemoteType(
            QUrlQuery(url).queryItemValue(
                QStringLiteral("rclone-remote-type")));
        readOnly = false;
        break;

    case RcloneLocation::Kind::DriveHub:
        name = location.remoteName();
        iconName = QStringLiteral("folder-gdrive");
        break;

    case RcloneLocation::Kind::DriveMyDrive:
        name = i18n("My Drive");
        iconName = QStringLiteral("user-home");
        break;

    case RcloneLocation::Kind::DriveSharedWithMe:
        name = i18n("Shared With Me");
        iconName = QStringLiteral("folder-publicshare");
        break;

    case RcloneLocation::Kind::DriveSharedDrives:
        name = i18n("Shared Drives");
        iconName = QStringLiteral("folder-cloud");
        break;

    case RcloneLocation::Kind::DriveTrash:
        name = i18n("Trash");
        iconName = QStringLiteral("user-trash-full");
        break;

    case RcloneLocation::Kind::DriveStarred:
        name = i18n("Starred");
        iconName = QStringLiteral("folder-favorites");
        break;

    case RcloneLocation::Kind::DriveSharedDrive:
        name = location.label().isEmpty()
            ? location.identifier()
            : location.label();
        iconName = QStringLiteral("folder-cloud");
        break;

    case RcloneLocation::Kind::ConfigureEntry:
        break;
    }

    RcloneNavigationEntry entry =
        RcloneNavigationEntry::virtualDirectory(
            name,
            url,
            iconName);
    entry.readOnly = readOnly;

    return entry;
}

bool isSyntheticDirectory(const RcloneLocation &location)
{
    if (location.isRoot() || location.remotePath().isEmpty()) {
        return true;
    }

    switch (location.kind()) {
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return true;

    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveMyDrive:
    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
    case RcloneLocation::Kind::DriveSharedDrive:
    case RcloneLocation::Kind::Root:
        return false;
    }

    return false;
}

RcloneListOptions statOptionsFor(const RcloneLocation &location)
{
    RcloneListOptions options;

    switch (location.kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
        options.extraArguments.append(
            QStringLiteral("--drive-shared-with-me"));
        break;

    case RcloneLocation::Kind::DriveStarred:
        options.extraArguments.append(
            QStringLiteral("--drive-starred-only"));
        break;

    case RcloneLocation::Kind::DriveSharedDrive:
        options.extraArguments.append(
            QStringLiteral("--drive-team-drive"));
        options.extraArguments.append(location.identifier());
        break;

    case RcloneLocation::Kind::DriveTrash:
        options.extraArguments.append(
            QStringLiteral("--drive-trashed-only"));
        break;

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveMyDrive:
    case RcloneLocation::Kind::DriveSharedDrives:
        break;
    }

    return options;
}

KIO::WorkerResult randomAccessNotImplemented()
{
    return KIO::WorkerResult::fail(
        KIO::ERR_UNSUPPORTED_ACTION,
        i18n("Random-access file operations are not implemented."));
}

} // namespace RcloneWorkerHelpers
