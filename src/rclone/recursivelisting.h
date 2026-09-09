/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rcloneclient.h"

#include <QMap>

#include <optional>

/**
 * Directory snapshots reconstructed from one complete `rclone lsjson
 * --recursive` response.
 *
 * rclone may return an item below a parent which the active provider filter
 * does not itself return. This index creates a synthetic parent only when it
 * leads to a listed item, so callers never publish an empty support branch.
 * The type is independent from KIO and can therefore be exercised with small
 * JSON-derived item fixtures.
 */
class RcloneRecursiveListing
{
public:
    using Directories = QMap<QString, QList<RcloneItem>>;

    [[nodiscard]] static std::optional<RcloneRecursiveListing>
    fromItems(const QList<RcloneItem> &recursiveItems, QString *error = nullptr);

    /// An empty list is a known empty directory; no value means that the path
    /// did not occur as a directory in the recursive result.
    [[nodiscard]] std::optional<QList<RcloneItem>> entries(const QString &relativePath) const;
    [[nodiscard]] const Directories &directories() const;

private:
    explicit RcloneRecursiveListing(Directories directories);

    Directories m_directories;
};
