/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

/**
 * The user-visible freshness contract for directory listings.
 *
 * RemoteOnly never serves a snapshot. RecentSnapshot permits only complete,
 * successful snapshots inside the configured freshness window; it is not an
 * offline mode.
 */
enum class DirectoryListingMode {
    RemoteOnly,
    RecentSnapshot,
};

struct DirectoryListingPolicy {
    static constexpr int DefaultFreshnessSeconds = 15;
    static constexpr int MinimumFreshnessSeconds = 1;
    static constexpr int MaximumFreshnessSeconds = 60;

    DirectoryListingMode mode = DirectoryListingMode::RecentSnapshot;
    int freshnessSeconds = DefaultFreshnessSeconds;

    [[nodiscard]] bool allowsSnapshots() const
    {
        return mode == DirectoryListingMode::RecentSnapshot && freshnessSeconds > 0;
    }
};

/**
 * Persistence for the listing policy, kept separate from snapshot storage.
 *
 * Callers which change the policy must clear persistent snapshots so the new
 * choice takes effect immediately in already running workers.
 */
namespace DirectoryListingPolicyStore
{
[[nodiscard]] DirectoryListingPolicy load();
void save(const DirectoryListingPolicy &policy);
}
