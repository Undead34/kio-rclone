/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "driveactiontarget.h"
#include "rclone/client.h"

#include <optional>

namespace DolphinDriveActions
{

/**
 * @brief Read-only Drive metadata lookup for the category-1 menu helper.
 *
 * Google Drive may omit a directory ID from `lsjson --stat`. In that case the
 * resolver lists the parent directory and accepts exactly one matching child.
 * It never calls `rclone link`, so it cannot widen a file's sharing settings.
 */
class DriveMetadataResolver
{
public:
    DriveMetadataResolver(RcloneClient &client, const RcloneContext &context);

    [[nodiscard]] bool isGoogleDriveRemote(const DriveActionTarget &target,
                                           QString *errorMessage) const;

    [[nodiscard]] std::optional<RcloneItem>
    item(const DriveActionTarget &target, QString *errorMessage) const;

    [[nodiscard]] std::optional<DriveActionTarget>
    targetFolder(const DriveActionTarget &target,
                 QString *errorMessage) const;

    [[nodiscard]] std::optional<QString>
    folderId(const DriveActionTarget &target, QString *errorMessage) const;

private:
    [[nodiscard]] std::optional<RcloneItem>
    itemFromParentListing(const DriveActionTarget &target,
                          QString *errorMessage) const;

    RcloneClient &m_client;
    const RcloneContext &m_context;
};

} // namespace DolphinDriveActions
