/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "client.h"

#include <QJsonObject>
#include <QString>

namespace RcloneClientHelpers
{

class CleanupContext final : public RcloneContext
{
public:
    [[nodiscard]] bool isCancelled() const override
    {
        return false;
    }
};

QString uploadStagingSpec(const QString &remoteDest);

RcloneStatus targetStillMatches(
    const RcloneTargetSnapshot &expected,
    const RcloneResponse<RcloneItem> &current);

RcloneErrorCode classifyProcessFailure(int exitCode,
                                       const QString &diagnostic);

void updateTransferStats(const QJsonObject &stats,
                         qint64 fileSize,
                         RcloneClient::TransferStats &progress);

} // namespace RcloneClientHelpers
