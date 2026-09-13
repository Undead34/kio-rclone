/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclone/client.h"
#include "rclone/location.h"
#include "rclone/navigationentry.h"

#include <KIO/WorkerBase>

#include <QString>
#include <QUrl>

namespace RcloneWorkerHelpers
{

class WorkerRcloneContext final : public RcloneContext
{
public:
    explicit WorkerRcloneContext(const KIO::WorkerBase &worker)
        : m_worker(worker)
    {
    }

    [[nodiscard]] bool isCancelled() const override
    {
        return m_worker.wasKilled();
    }

private:
    const KIO::WorkerBase &m_worker;
};

KIO::WorkerResult malformedUrlResult(const QUrl &url);

KIO::WorkerResult rcloneFailure(const RcloneError &error,
                                const QUrl &url,
                                int fallbackError);

KIO::WorkerResult listFailure(const RcloneError &error,
                              const QUrl &url);

KIO::WorkerResult statFailure(const RcloneError &error,
                              const QUrl &url);

QString iconForRemoteType(const QString &type);

KIO::UDSEntry makeEntry(const RcloneNavigationEntry &entry);

QUrl withItemIdentity(QUrl url,
                      const RcloneItem &item);

QUrl withRemoteType(QUrl url,
                    const QString &remoteType);

RcloneNavigationEntry virtualDirectoryForStat(
    const RcloneLocation &location,
    const QUrl &url);

bool isSyntheticDirectory(const RcloneLocation &location);

RcloneListOptions statOptionsFor(const RcloneLocation &location);

KIO::WorkerResult randomAccessNotImplemented();

} // namespace RcloneWorkerHelpers
