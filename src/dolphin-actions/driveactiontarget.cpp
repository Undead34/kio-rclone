/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "driveactiontarget.h"

#include <KLocalizedString>

#include <QUrlQuery>

#include <utility>

namespace {

bool isValidDriveIdentifier(const QString &identifier)
{
    if (identifier.isEmpty()) {
        return false;
    }

    for (const QChar character : identifier) {
        if (character.isLetterOrNumber()
            || character == QLatin1Char('_')
            || character == QLatin1Char('-')) {
            continue;
        }

        return false;
    }

    return true;
}

std::optional<QString> driveIdFromQuery(const QUrlQuery &query,
                                        const QString &key,
                                        QString *errorMessage)
{
    if (!query.hasQueryItem(key)) {
        return QString{};
    }

    const QString identifier = query.queryItemValue(key);

    if (!isValidDriveIdentifier(identifier)) {
        if (errorMessage) {
            *errorMessage = i18n("The selected item has an invalid Google Drive identifier.");
        }

        return std::nullopt;
    }

    return identifier;
}

void setError(QString *destination, const QString &message)
{
    if (destination) {
        *destination = message;
    }
}

} // namespace

namespace DolphinDriveActions
{

bool DriveActionTarget::hasGoogleDriveRemoteType(const QUrl &url)
{
    if (url.scheme() != QLatin1String("rclone")) {
        return false;
    }

    const QUrlQuery query(url);
    const QStringList values = query.allQueryItemValues(
        QStringLiteral("rclone-remote-type"));

    return values.size() == 1 && values.constFirst() == QLatin1String("drive");
}

DriveActionTarget::DriveActionTarget(QString remoteName,
                                     QString remotePath,
                                     RcloneLocation::Kind view,
                                     QString sharedDriveId,
                                     QString itemId,
                                     QString originalId)
    : m_remoteName(std::move(remoteName))
    , m_remotePath(std::move(remotePath))
    , m_view(view)
    , m_sharedDriveId(std::move(sharedDriveId))
    , m_itemId(std::move(itemId))
    , m_originalId(std::move(originalId))
{
}

std::optional<DriveActionTarget>
DriveActionTarget::fromUrl(const QUrl &url, QString *errorMessage)
{
    const QUrlQuery query(url);
    const auto itemId = driveIdFromQuery(
        query,
        QStringLiteral("rclone-id"),
        errorMessage);

    if (!itemId) {
        return std::nullopt;
    }

    const auto originalId = driveIdFromQuery(
        query,
        QStringLiteral("rclone-orig-id"),
        errorMessage);

    if (!originalId) {
        return std::nullopt;
    }

    const auto location = RcloneLocation::parse(url);

    if (!location) {
        setError(errorMessage, i18n("The selected item is not a valid rclone location."));
        return std::nullopt;
    }

    switch (location->kind()) {
    case RcloneLocation::Kind::DriveMyDrive:
        return DriveActionTarget(
            location->remoteName(),
            location->remotePath(),
            location->kind(),
            {},
            *itemId,
            *originalId);

    case RcloneLocation::Kind::DriveSharedDrive:
        if (!isValidDriveIdentifier(location->identifier())) {
            setError(errorMessage, i18n("The selected Shared Drive has an invalid identifier."));
            return std::nullopt;
        }

        return DriveActionTarget(
            location->remoteName(),
            location->remotePath(),
            location->kind(),
            location->identifier(),
            *itemId,
            *originalId);

    case RcloneLocation::Kind::DriveSharedWithMe:
    case RcloneLocation::Kind::DriveTrash:
    case RcloneLocation::Kind::DriveStarred:
        return DriveActionTarget(
            location->remoteName(),
            location->remotePath(),
            location->kind(),
            {},
            *itemId,
            *originalId);

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        break;
    }

    setError(
        errorMessage,
        i18n("Google Drive actions are available for Drive items, My Drive, and individual Shared Drives."));
    return std::nullopt;
}

QString DriveActionTarget::remoteName() const
{
    return m_remoteName;
}

QString DriveActionTarget::remotePath() const
{
    return m_remotePath;
}

RcloneLocation::Kind DriveActionTarget::view() const
{
    return m_view;
}

QString DriveActionTarget::itemId() const
{
    return m_itemId.isEmpty() ? m_originalId : m_itemId;
}

bool DriveActionTarget::isRoot() const
{
    return m_remotePath.isEmpty() && supportsPathFallback();
}

bool DriveActionTarget::supportsPathFallback() const
{
    return m_view == RcloneLocation::Kind::DriveMyDrive
        || m_view == RcloneLocation::Kind::DriveSharedDrive;
}

bool DriveActionTarget::allowsCreation() const
{
    return m_view == RcloneLocation::Kind::DriveMyDrive
        || m_view == RcloneLocation::Kind::DriveSharedDrive;
}

QString DriveActionTarget::statSpec() const
{
    if (m_view == RcloneLocation::Kind::DriveSharedDrive) {
        return m_remoteName
            + QStringLiteral(",team_drive=")
            + m_sharedDriveId
            + QStringLiteral(",root_folder_id=:")
            + m_remotePath;
    }

    return m_remoteName + QLatin1Char(':') + m_remotePath;
}

QString DriveActionTarget::listingSpec() const
{
    return m_remoteName + QLatin1Char(':') + m_remotePath;
}

QStringList DriveActionTarget::listingArguments() const
{
    switch (m_view) {
    case RcloneLocation::Kind::DriveSharedDrive:
        return {
            QStringLiteral("--drive-team-drive"),
            m_sharedDriveId,
        };

    case RcloneLocation::Kind::DriveSharedWithMe:
        return {QStringLiteral("--drive-shared-with-me")};

    case RcloneLocation::Kind::DriveStarred:
        return {QStringLiteral("--drive-starred-only")};

    case RcloneLocation::Kind::DriveTrash:
        return {QStringLiteral("--drive-trashed-only")};

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveMyDrive:
    case RcloneLocation::Kind::DriveSharedDrives:
        break;
    }

    return {};
}

QString DriveActionTarget::leafName() const
{
    const qsizetype separator = m_remotePath.lastIndexOf(QLatin1Char('/'));

    if (separator < 0) {
        return m_remotePath;
    }

    return m_remotePath.mid(separator + 1);
}

DriveActionTarget DriveActionTarget::parent() const
{
    const qsizetype separator = m_remotePath.lastIndexOf(QLatin1Char('/'));
    const QString parentPath = separator < 0
        ? QString{}
        : m_remotePath.left(separator);

    return DriveActionTarget(
        m_remoteName,
        parentPath,
        m_view,
        m_sharedDriveId);
}

QString DriveActionTarget::rootFolderId() const
{
    if (m_view == RcloneLocation::Kind::DriveSharedDrive) {
        return m_sharedDriveId;
    }

    return {};
}

} // namespace DolphinDriveActions
