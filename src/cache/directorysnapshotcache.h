/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclone/models.h"

#include <QList>

#include <memory>
#include <optional>

class KSharedDataCache;

/**
 * A small, shared on-disk cache of complete successful rclone directory
 * listings. Entries are intentionally short-lived: this is a fast reopening
 * path for Dolphin, not an offline filesystem.
 *
 * The backing KSharedDataCache is best-effort and evictable. Its contents must
 * therefore never authorize mutations or contain credentials. See
 * https://api.kde.org/kcoreaddons-module.html
 */
class DirectorySnapshotCache
{
public:
    static constexpr qsizetype MaximumItemCount = 4096;

    DirectorySnapshotCache();
    ~DirectorySnapshotCache();

    DirectorySnapshotCache(const DirectorySnapshotCache &) = delete;
    DirectorySnapshotCache &operator=(const DirectorySnapshotCache &) = delete;

    /// Returns a complete snapshot inside freshnessSeconds or no value. A miss
    /// and an invalid, corrupt, or expired entry intentionally look alike.
    /// The caller decides whether the current request may use a snapshot.
    [[nodiscard]] std::optional<QList<RcloneItem>> load(const QString &remote,
                                                        const QString &remotePath,
                                                        int freshnessSeconds);

    /// Stores one complete, successfully listed directory. Callers must not
    /// pass partial output after cancellation or a backend error.
    [[nodiscard]] bool store(const QString &remote, const QString &remotePath, const QList<RcloneItem> &items);

    /// Removes snapshots visible through this cache instance.
    void clear();

    /// Opens the shared cache and removes all persistent snapshots.
    static void clearPersistent();

private:
    std::unique_ptr<KSharedDataCache> m_cache;
};
