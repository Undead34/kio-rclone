/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "directorylistingpolicy.h"

#include <KConfig>
#include <KConfigGroup>

#include <QtGlobal>

namespace
{
DirectoryListingPolicy normalized(DirectoryListingPolicy value)
{
    value.freshnessSeconds = qBound(DirectoryListingPolicy::MinimumFreshnessSeconds,
                                    value.freshnessSeconds,
                                    DirectoryListingPolicy::MaximumFreshnessSeconds);
    return value;
}

KConfigGroup policyGroup(KConfig &config)
{
    return KConfigGroup(&config, QStringLiteral("DirectoryListingCache"));
}
} // namespace

DirectoryListingPolicy DirectoryListingPolicyStore::load()
{
    KConfig config(QStringLiteral("kiorclonerc"), KConfig::NoGlobals);
    const KConfigGroup group = policyGroup(config);

    DirectoryListingPolicy policy;
    policy.mode = group.readEntry(QStringLiteral("Mode"), QStringLiteral("fresh")) == QLatin1String("strict")
        ? DirectoryListingMode::RemoteOnly
        : DirectoryListingMode::RecentSnapshot;
    policy.freshnessSeconds = group.readEntry(QStringLiteral("FreshnessSeconds"), DirectoryListingPolicy::DefaultFreshnessSeconds);
    return normalized(policy);
}

void DirectoryListingPolicyStore::save(const DirectoryListingPolicy &value)
{
    const DirectoryListingPolicy policy = normalized(value);
    KConfig config(QStringLiteral("kiorclonerc"), KConfig::NoGlobals);
    KConfigGroup group = policyGroup(config);
    group.writeEntry(QStringLiteral("Mode"), policy.mode == DirectoryListingMode::RemoteOnly ? QStringLiteral("strict") : QStringLiteral("fresh"));
    group.writeEntry(QStringLiteral("FreshnessSeconds"), policy.freshnessSeconds);
    group.sync();
}
