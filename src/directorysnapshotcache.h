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
    Strict,
    Fresh,
};

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

    [[nodiscard]] std::optional<QList<RcloneItem>> load(const QString &remote, const QString &remotePath);
    [[nodiscard]] bool store(const QString &remote, const QString &remotePath, const QList<RcloneItem> &items);
    void clear();

    [[nodiscard]] static DirectoryCachePolicy policy();
    static void setPolicy(const DirectoryCachePolicy &policy);
    static void clearPersistent();

private:
    std::unique_ptr<KSharedDataCache> m_cache;
};
