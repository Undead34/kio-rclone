/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclone/location.h"

#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace DolphinDriveActions
{

/**
 * @brief A Google Drive item addressed through the public rclone:/ namespace.
 *
 * Category-1 Dolphin service menus receive a URL rather than a local FUSE
 * path. This type preserves the selected Drive view and turns it into the
 * rclone requests needed only by the action helper; it does not change the
 * KIO worker's navigation or mutation behaviour.
 */
class DriveActionTarget
{
public:
    /**
     * @brief Checks the URL marker used by the native Dolphin action plugin.
     *
     * Service-menu metadata cannot inspect URL query items. The category-2
     * plugin uses this marker before it creates any action, so Google Drive
     * entries are the only rclone entries which expose the menu.
     */
    [[nodiscard]] static bool hasGoogleDriveRemoteType(const QUrl &url);

    [[nodiscard]] static std::optional<DriveActionTarget>
    fromUrl(const QUrl &url, QString *errorMessage = nullptr);

    [[nodiscard]] QString remoteName() const;
    [[nodiscard]] QString remotePath() const;
    [[nodiscard]] RcloneLocation::Kind view() const;

    /**
     * @brief The Drive ID embedded by the KIO worker in `UDS_URL`, if present.
     *
     * `rclone-id` is authoritative for normal items. `rclone-orig-id` is a
     * safe fallback for entries which only expose an original object ID.
     */
    [[nodiscard]] QString itemId() const;

    /**
     * @brief Whether this is a physical My Drive or Shared Drive root.
     *
     * Filtered Drive views also have an empty public path at their top level,
     * but that path is not a Google Drive folder ID and must not be treated as
     * My Drive's root.
     */
    [[nodiscard]] bool isRoot() const;

    /**
     * @brief Whether an older URL can be resolved from its public path.
     *
     * Shared With Me, Starred, and Trash need the item identity carried by
     * UDS_URL. Their synthetic paths are not safe fallbacks for `rclone
     * lsjson --stat`.
     */
    [[nodiscard]] bool supportsPathFallback() const;

    [[nodiscard]] bool allowsCreation() const;

    /**
     * @brief Builds a path for `rclone lsjson --stat`.
     *
     * The shared-drive form keeps the Drive identity in the rclone connection
     * string because the KIO URL intentionally contains only public paths.
     */
    [[nodiscard]] QString statSpec() const;

    /**
     * @brief Builds a directory path for `rclone lsjson` fallback lookups.
     */
    [[nodiscard]] QString listingSpec() const;

    /**
     * @brief Adds the rclone flags needed to list this Drive view.
     */
    [[nodiscard]] QStringList listingArguments() const;

    [[nodiscard]] QString leafName() const;
    [[nodiscard]] DriveActionTarget parent() const;

    /**
     * @brief ID for a root whose Drive identity is already known.
     *
     * An empty value is a valid My Drive root, for which Google Workspace
     * create URLs deliberately omit the folder parameter.
     */
    [[nodiscard]] QString rootFolderId() const;

private:
    DriveActionTarget(QString remoteName,
                      QString remotePath,
                      RcloneLocation::Kind view,
                      QString sharedDriveId = {},
                      QString itemId = {},
                      QString originalId = {});

    QString m_remoteName;
    QString m_remotePath;
    RcloneLocation::Kind m_view;
    QString m_sharedDriveId;
    QString m_itemId;
    QString m_originalId;
};

} // namespace DolphinDriveActions
