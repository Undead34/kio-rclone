/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "url.h"

#include <QUrl>
#include <QString>

#include <optional>

class RcloneLocation
{
public:
    enum class Kind {
        Root,
        ConfigureEntry,
        Standard,

        DriveHub,
        DriveMyDrive,
        DriveSharedWithMe,
        DriveSharedDrives,
        DriveTrash,
        DriveStarred,
        DriveSharedDrive,
    };

    [[nodiscard]] static std::optional<RcloneLocation>
    parse(const QUrl &url);

    [[nodiscard]] static QUrl standardUrl(const QString &remoteName,
                                          const QString &remotePath = {});

    [[nodiscard]] static QUrl driveHubUrl(const QString &remoteName);

    [[nodiscard]] static QUrl driveViewUrl(const QString &remoteName,
                                           Kind kind,
                                           const QString &remotePath = {});

    [[nodiscard]] static QUrl driveSharedDriveUrl(const QString &remoteName,
                                                  const QString &driveId,
                                                  const QString &driveName,
                                                  const QString &remotePath = {});

    [[nodiscard]] Kind kind() const;

    [[nodiscard]] bool isRoot() const;
    [[nodiscard]] bool isConfigureEntry() const;
    [[nodiscard]] bool isDriveVirtual() const;

    [[nodiscard]] QString remoteName() const;
    [[nodiscard]] QString remotePath() const;
    [[nodiscard]] QString toCliSpec() const;
    [[nodiscard]] QUrl toQUrl() const;

    [[nodiscard]] QString identifier() const;
    [[nodiscard]] QString label() const;

private:
    RcloneLocation(RcloneUrl base,
                   Kind kind,
                   QString identifier = {},
                   QString label = {});

    RcloneUrl m_base;
    Kind m_kind = Kind::Standard;
    QString m_identifier;
    QString m_label;
};
