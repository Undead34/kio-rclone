/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "location.h"

#include <QChar>

#include <utility>

namespace
{
constexpr auto MyDrive = "my-drive";
constexpr auto SharedWithMe = "shared-with-me";
constexpr auto SharedDrives = "shared-drives";
constexpr auto Trash = "trash";
constexpr auto Starred = "starred";
constexpr auto Folders = "folders";

QString appendPath(const QString &rootSpec, const QString &path)
{
    return rootSpec + path;
}

QString appendCachePath(const QString &nameSpace, const QString &path)
{
    if (nameSpace.isEmpty()) {
        return path;
    }
    return path.isEmpty() ? nameSpace : nameSpace + QLatin1Char('/') + path;
}
} // namespace

RcloneLocation::RcloneLocation(Kind kind,
                               QString remote,
                               QString relativePath,
                               QString identifier,
                               QString rootSpec,
                               QString cacheNamespace,
                               QString mutationScope,
                               bool writable)
    : m_kind(kind)
    , m_remote(std::move(remote))
    , m_relativePath(std::move(relativePath))
    , m_identifier(std::move(identifier))
    , m_rootSpec(std::move(rootSpec))
    , m_cacheNamespace(std::move(cacheNamespace))
    , m_mutationScope(std::move(mutationScope))
    , m_writable(writable)
{
}

RcloneLocation RcloneLocation::standard(const QString &remote, const QString &remotePath)
{
    return {Kind::Standard,
            remote,
            remotePath,
            {},
            remote + QLatin1Char(':'),
            {},
            QStringLiteral("standard:") + remote,
            true};
}

std::optional<RcloneLocation> RcloneLocation::googleDrive(const QString &remote, const QString &remotePath)
{
    if (remote.isEmpty() || !isValidRelativePath(remotePath)) {
        return std::nullopt;
    }

    if (remotePath.isEmpty()) {
        return RcloneLocation{Kind::DriveHub,
                              remote,
                              {},
                              {},
                              {},
                              QStringLiteral("drive/hub"),
                              {},
                              false};
    }

    const QStringList parts = remotePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString first = parts.constFirst();
    const QString relative = parts.mid(1).join(QLatin1Char('/'));

    if (first == QLatin1String(MyDrive)) {
        return RcloneLocation{Kind::DriveMyDrive,
                              remote,
                              relative,
                              {},
                              remote + QLatin1Char(':'),
                              QStringLiteral("drive/my-drive"),
                              QStringLiteral("drive:my-drive"),
                              true};
    }
    if (first == QLatin1String(SharedWithMe)) {
        return RcloneLocation{Kind::DriveSharedWithMe,
                              remote,
                              relative,
                              {},
                              remote + QStringLiteral(",shared_with_me:"),
                              QStringLiteral("drive/shared-with-me"),
                              {},
                              false};
    }
    if (first == QLatin1String(Trash)) {
        return RcloneLocation{Kind::DriveTrash,
                              remote,
                              relative,
                              {},
                              remote + QStringLiteral(",trashed_only:"),
                              QStringLiteral("drive/trash"),
                              {},
                              false};
    }
    if (first == QLatin1String(Starred)) {
        return RcloneLocation{Kind::DriveStarred,
                              remote,
                              relative,
                              {},
                              remote + QStringLiteral(",starred_only:"),
                              QStringLiteral("drive/starred"),
                              {},
                              false};
    }
    if (first == QLatin1String(SharedDrives)) {
        if (parts.size() == 1) {
            return RcloneLocation{Kind::DriveSharedDrives,
                                  remote,
                                  {},
                                  {},
                                  {},
                                  QStringLiteral("drive/shared-drives"),
                                  {},
                                  false};
        }

        const QString id = parts.at(1);
        if (!isValidIdentifier(id)) {
            return std::nullopt;
        }
        const QString driveRelative = parts.mid(2).join(QLatin1Char('/'));
        return RcloneLocation{Kind::DriveSharedDrive,
                              remote,
                              driveRelative,
                              id,
                              remote + QStringLiteral(",team_drive=") + id + QStringLiteral(",root_folder_id=:"),
                              QStringLiteral("drive/shared-drives/") + id,
                              QStringLiteral("drive:shared-drive:") + id,
                              true};
    }
    if (first == QLatin1String(Folders)) {
        if (parts.size() == 1) {
            return RcloneLocation{Kind::DriveFolders,
                                  remote,
                                  {},
                                  {},
                                  {},
                                  QStringLiteral("drive/folders"),
                                  {},
                                  false};
        }

        const QString id = parts.at(1);
        if (!isValidIdentifier(id)) {
            return std::nullopt;
        }
        const QString folderRelative = parts.mid(2).join(QLatin1Char('/'));
        return RcloneLocation{Kind::DriveFolder,
                              remote,
                              folderRelative,
                              id,
                              remote + QStringLiteral(",root_folder_id=") + id + QLatin1Char(':'),
                              QStringLiteral("drive/folders/") + id,
                              QStringLiteral("drive:folder:") + id,
                              true};
    }

    return std::nullopt;
}

RcloneLocation::Kind RcloneLocation::kind() const
{
    return m_kind;
}

QString RcloneLocation::remote() const
{
    return m_remote;
}

QString RcloneLocation::relativePath() const
{
    return m_relativePath;
}

QString RcloneLocation::identifier() const
{
    return m_identifier;
}

QString RcloneLocation::rcloneSpec() const
{
    return hasRcloneTarget() ? appendPath(m_rootSpec, m_relativePath) : QString();
}

QString RcloneLocation::rcloneRootSpec() const
{
    return m_rootSpec;
}

QString RcloneLocation::parentSpec() const
{
    if (!hasRcloneTarget()) {
        return {};
    }
    return appendPath(m_rootSpec, m_relativePath.section(QLatin1Char('/'), 0, -2));
}

QString RcloneLocation::leafName() const
{
    return m_relativePath.section(QLatin1Char('/'), -1);
}

QString RcloneLocation::cachePath() const
{
    return appendCachePath(m_cacheNamespace, m_relativePath);
}

QString RcloneLocation::mutationScope() const
{
    return m_mutationScope;
}

bool RcloneLocation::hasRcloneTarget() const
{
    return !m_rootSpec.isEmpty();
}

bool RcloneLocation::isVirtualDirectory() const
{
    return m_kind == Kind::DriveHub || m_kind == Kind::DriveSharedDrives || m_kind == Kind::DriveFolders;
}

bool RcloneLocation::canWrite() const
{
    return m_writable;
}

bool RcloneLocation::isDriveHub() const
{
    return m_kind == Kind::DriveHub;
}

bool RcloneLocation::isDriveTrash() const
{
    return m_kind == Kind::DriveTrash;
}

bool RcloneLocation::isSharedDrivesRoot() const
{
    return m_kind == Kind::DriveSharedDrives;
}

bool RcloneLocation::isFoldersRoot() const
{
    return m_kind == Kind::DriveFolders;
}

bool RcloneLocation::isPhysicalRoot() const
{
    return hasRcloneTarget() && m_relativePath.isEmpty();
}

bool RcloneLocation::sharesMutationScope(const RcloneLocation &other) const
{
    return !m_mutationScope.isEmpty() && m_mutationScope == other.m_mutationScope;
}

RcloneLocation RcloneLocation::withRelativePath(const QString &path) const
{
    RcloneLocation copy = *this;
    copy.m_relativePath = path;
    return copy;
}

QList<RcloneLocation::HubEntry> RcloneLocation::driveHubEntries()
{
    return {
        {Kind::DriveMyDrive, QString::fromLatin1(MyDrive), QStringLiteral("user-home"), true},
        {Kind::DriveSharedWithMe, QString::fromLatin1(SharedWithMe), QStringLiteral("folder-publicshare"), false},
        {Kind::DriveSharedDrives, QString::fromLatin1(SharedDrives), QStringLiteral("folder-cloud"), false},
        {Kind::DriveTrash, QString::fromLatin1(Trash), QStringLiteral("user-trash-full"), false},
        {Kind::DriveStarred, QString::fromLatin1(Starred), QStringLiteral("folder-favorites"), false},
    };
}

bool RcloneLocation::isValidRelativePath(const QString &path)
{
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String(".") || part == QLatin1String("..") || part.contains(QChar::Null)) {
            return false;
        }
    }
    return !path.startsWith(QLatin1Char('/'));
}

bool RcloneLocation::isValidIdentifier(const QString &identifier)
{
    if (identifier.isEmpty()) {
        return false;
    }
    for (const QChar character : identifier) {
        if (character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_')) {
            continue;
        }
        return false;
    }
    return true;
}
