/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclone/client.h"
#include "rclone/navigation.h"
#include "rclone/remotefile.h"
#include "rclone/syncrepository.h"

#include <KIO/WorkerBase>

#include <QByteArray>
#include <QDateTime>
#include <QIODevice>
#include <QUrl>

#include <memory>

/**
 * @brief KIO Worker para remotos administrados mediante rclone.
 *
 * La navegación y las operaciones remotas se delegan a las capas rclone.
 * Las operaciones de archivo aleatorias utilizan una copia local completa
 * administrada por RcloneRemoteFile.
 */
class RcloneWorker : public KIO::WorkerBase
{
public:
    RcloneWorker(const QByteArray &protocol,
                 const QByteArray &poolSocket,
                 const QByteArray &appSocket);

    // Navigation / conventional KIO operations

    KIO::WorkerResult listDir(const QUrl &url) override;

    KIO::WorkerResult stat(const QUrl &url) override;

    KIO::WorkerResult mimetype(const QUrl &url) override;

    KIO::WorkerResult get(const QUrl &url) override;

    KIO::WorkerResult put(const QUrl &url,
                          int permissions,
                          KIO::JobFlags flags) override;

    KIO::WorkerResult mkdir(const QUrl &url,
                            int permissions) override;

    KIO::WorkerResult rename(const QUrl &src,
                             const QUrl &dest,
                             KIO::JobFlags flags) override;

    KIO::WorkerResult copy(const QUrl &src,
                           const QUrl &dest,
                           int permissions,
                           KIO::JobFlags flags) override;

    KIO::WorkerResult del(const QUrl &url,
                          bool isFile) override;

    KIO::WorkerResult fileSystemFreeSpace(const QUrl &url) override;

    // Random access operations

    KIO::WorkerResult open(const QUrl &url,
                           QIODevice::OpenMode mode) override;

    KIO::WorkerResult read(KIO::filesize_t size) override;

    KIO::WorkerResult write(const QByteArray &data) override;

    KIO::WorkerResult seek(KIO::filesize_t offset) override;

    KIO::WorkerResult truncate(KIO::filesize_t length) override;

    KIO::WorkerResult close() override;

    KIO::WorkerResult setModificationTime(const QUrl &url,
                                          const QDateTime &mtime) override;

private:
    RcloneClient m_client;

    RcloneNavigation m_navigation;

    RcloneSyncRepository m_syncRepository;

    std::unique_ptr<RcloneRemoteFile> m_openFile;

    QUrl m_openUrl;
};
