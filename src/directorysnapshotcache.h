/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclonebackend.h"

#include <QList>

#include <memory>
#include <optional>

class KSharedDataCache;

enum class DirectoryCacheMode {
    /// Every listing is resolved by rclone; no snapshot is served.
    Strict,
    /// A complete snapshot may satisfy a normal read-only directory request.
    Fresh,
};

/// User-configurable freshness policy. It deliberately has no "offline" mode:
/// this cache shortens reopen latency, but is never a remote filesystem index.
struct DirectoryCachePolicy {
    DirectoryCacheMode mode = DirectoryCacheMode::Fresh;
    int freshnessSeconds = 15;

    [[nodiscard]] bool allowsSnapshots() const
    {
        return mode == DirectoryCacheMode::Fresh && freshnessSeconds > 0;
    }
};

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
    static constexpr int DefaultFreshnessSeconds = 15;
    static constexpr int MinimumFreshnessSeconds = 1;
    static constexpr int MaximumFreshnessSeconds = 60;
    static constexpr qsizetype MaximumItemCount = 4096;

    DirectorySnapshotCache();
    ~DirectorySnapshotCache();

    DirectorySnapshotCache(const DirectorySnapshotCache &) = delete;
    DirectorySnapshotCache &operator=(const DirectorySnapshotCache &) = delete;

    /// Returns a complete, still-fresh snapshot or no value. A miss and an
    /// invalid/corrupt/expired entry intentionally have the same result.
    [[nodiscard]] std::optional<QList<RcloneItem>> load(const QString &remote, const QString &remotePath);

    /// Stores one complete, successfully listed directory. Callers must not
    /// pass partial output after cancellation or a backend error.
    [[nodiscard]] bool store(const QString &remote, const QString &remotePath, const QList<RcloneItem> &items);

    /// Removes snapshots visible through this cache instance.
    void clear();

    /// Reads or changes the global policy. Setting a policy clears existing
    /// snapshots so a stricter choice takes effect immediately across workers.
    [[nodiscard]] static DirectoryCachePolicy policy();
    static void setPolicy(const DirectoryCachePolicy &policy);

    /// Opens the shared cache and removes all persistent snapshots.
    static void clearPersistent();

private:
    std::unique_ptr<KSharedDataCache> m_cache;
};
