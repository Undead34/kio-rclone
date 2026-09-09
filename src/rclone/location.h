/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * Maps a public rclone:/ location to the rclone connection string that serves
 * it. Most remotes use the standard remote:path mapping. Google Drive gains a
 * small virtual hub whose stable path segments select a Drive view.
 *
 * This is deliberately a pure value type: it does not invoke rclone, KIO, or
 * configuration APIs. The worker decides whether a remote is a Google Drive
 * backend, then asks this class to interpret its URL path. The resulting
 * connection strings follow https://rclone.org/docs/#connection-strings and
 * https://rclone.org/drive/.
 */
class RcloneLocation
{
public:
    enum class Kind {
        Standard,
        DriveHub,
        DriveMyDrive,
        DriveSharedWithMe,
        DriveSharedDrives,
        DriveSharedDrive,
        DriveTrash,
        DriveStarred,
        DriveFolders,
        DriveFolder,
    };

    struct HubEntry {
        Kind kind;
        QString name;
        QString iconName;
        bool writable = false;
    };

    [[nodiscard]] static RcloneLocation standard(const QString &remote, const QString &remotePath);
    [[nodiscard]] static std::optional<RcloneLocation> googleDrive(const QString &remote, const QString &remotePath);

    [[nodiscard]] Kind kind() const;
    [[nodiscard]] QString remote() const;
    [[nodiscard]] QString relativePath() const;
    [[nodiscard]] QString identifier() const;

    /// The rclone connection string for this exact location, or an empty
    /// string for a purely virtual directory such as the Drive hub.
    [[nodiscard]] QString rcloneSpec() const;
    /// The effective remote root. This is used for ID-based operations that
    /// must stay in the same Drive namespace as the visible location.
    [[nodiscard]] QString rcloneRootSpec() const;
    [[nodiscard]] QString parentSpec() const;
    [[nodiscard]] QString leafName() const;

    /// Stable cache namespace. It intentionally includes the Drive view, so
    /// `my-drive/report.pdf` cannot collide with a filtered Drive view of the
    /// same path.
    [[nodiscard]] QString cachePath() const;
    [[nodiscard]] QString mutationScope() const;

    [[nodiscard]] bool hasRcloneTarget() const;
    [[nodiscard]] bool isVirtualDirectory() const;
    [[nodiscard]] bool canWrite() const;
    [[nodiscard]] bool isDriveHub() const;
    [[nodiscard]] bool isSharedDrivesRoot() const;
    [[nodiscard]] bool isFoldersRoot() const;
    [[nodiscard]] bool isPhysicalRoot() const;
    [[nodiscard]] bool sharesMutationScope(const RcloneLocation &other) const;

    /// Returns a sibling location in the same physical namespace. It is used
    /// to validate ancestors without reinterpreting a public URL path.
    [[nodiscard]] RcloneLocation withRelativePath(const QString &path) const;

    [[nodiscard]] static QList<HubEntry> driveHubEntries();

private:
    RcloneLocation(Kind kind,
                   QString remote,
                   QString relativePath,
                   QString identifier,
                   QString rootSpec,
                   QString cacheNamespace,
                   QString mutationScope,
                   bool writable);

    [[nodiscard]] static bool isValidRelativePath(const QString &path);
    [[nodiscard]] static bool isValidIdentifier(const QString &identifier);

    Kind m_kind = Kind::Standard;
    QString m_remote;
    QString m_relativePath;
    QString m_identifier;
    QString m_rootSpec;
    QString m_cacheNamespace;
    QString m_mutationScope;
    bool m_writable = true;
};
