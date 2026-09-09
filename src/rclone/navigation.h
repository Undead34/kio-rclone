/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "client.h"
#include "location.h"
#include "navigationentry.h"

#include <functional>

class RcloneNavigation
{
public:
    using EntryCallback = std::function<bool(const RcloneNavigationEntry &)>;

    explicit RcloneNavigation(RcloneClient &client);

    [[nodiscard]] RcloneStatus listRoot(const EntryCallback &onEntry,
                                        const RcloneContext &ctx) const;

    [[nodiscard]] RcloneStatus list(const RcloneLocation &location,
                                    const EntryCallback &onEntry,
                                    const RcloneContext &ctx) const;

private:
    [[nodiscard]] RcloneResponse<QString> remoteType(const QString &remoteName,
                                                     const RcloneContext &ctx) const;

    [[nodiscard]] RcloneStatus listStandard(const RcloneLocation &location,
                                            const EntryCallback &onEntry,
                                            const RcloneContext &ctx) const;

    [[nodiscard]] RcloneStatus listDriveHub(const RcloneLocation &location,
                                            const EntryCallback &onEntry) const;

    [[nodiscard]] RcloneStatus listDriveView(const RcloneLocation &location,
                                             const EntryCallback &onEntry,
                                             const RcloneContext &ctx) const;

    [[nodiscard]] RcloneStatus listSharedDrives(const RcloneLocation &location,
                                                const EntryCallback &onEntry,
                                                const RcloneContext &ctx) const;

    [[nodiscard]] RcloneStatus listDriveTrash(const RcloneLocation &location,
                                              const EntryCallback &onEntry,
                                              const RcloneContext &ctx) const;

    RcloneClient &m_client;
};
