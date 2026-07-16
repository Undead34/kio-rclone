/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclonebackend.h"

#include <KIO/WorkerBase>
#include <QTemporaryFile>

#include <memory>

class QProcess;
class RcloneUrl;

class RcloneWorker : public KIO::WorkerBase
{
public:
    RcloneWorker(const QByteArray &protocol, const QByteArray &poolSocket, const QByteArray &appSocket);

    KIO::WorkerResult listDir(const QUrl &url) override;
    KIO::WorkerResult stat(const QUrl &url) override;
    KIO::WorkerResult mimetype(const QUrl &url) override;
    KIO::WorkerResult get(const QUrl &url) override;
    KIO::WorkerResult put(const QUrl &url, int permissions, KIO::JobFlags flags) override;
    KIO::WorkerResult mkdir(const QUrl &url, int permissions) override;
    KIO::WorkerResult rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags) override;
    KIO::WorkerResult copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags) override;
    KIO::WorkerResult del(const QUrl &url, bool isFile) override;
    KIO::WorkerResult fileSystemFreeSpace(const QUrl &url) override;

private:
    [[nodiscard]] KIO::UDSEntry rootEntry() const;
    [[nodiscard]] KIO::UDSEntry configureEntry() const;
    [[nodiscard]] KIO::UDSEntry remoteEntry(const QString &name, bool currentDirectory = false) const;
    [[nodiscard]] KIO::UDSEntry itemEntry(const RcloneItem &item) const;
    [[nodiscard]] KIO::WorkerResult ensureBackend() const;
    [[nodiscard]] KIO::WorkerResult launchConfiguration();
    [[nodiscard]] KIO::WorkerResult commandResult(const RcloneResult &result, int fallbackError, const QUrl &url) const;
    [[nodiscard]] KIO::WorkerResult errorResult(const QString &message, int fallbackError, const QUrl &url) const;
    [[nodiscard]] RcloneResult runCommand(const QStringList &arguments, int timeoutMs = 120000) const;
    [[nodiscard]] RcloneResult runUpload(const QString &localPath, const QString &remoteSpec);
    [[nodiscard]] bool remoteExists(const QString &remote, QString *error = nullptr) const;
    [[nodiscard]] bool destinationExists(const QString &remoteSpec, std::optional<RcloneItem> *item, QString *error) const;
    [[nodiscard]] std::optional<RcloneItem> sourceItem(const RcloneUrl &url, QString *error = nullptr) const;
    [[nodiscard]] KIO::WorkerResult cacheRemoteFile(const RcloneUrl &url, RcloneItem &item);
    [[nodiscard]] KIO::WorkerResult resolveUnknownSize(const RcloneUrl &url, RcloneItem &item);
    [[nodiscard]] bool cachedDownloadMatches(const RcloneUrl &url, const RcloneItem &item) const;
    [[nodiscard]] KIO::WorkerResult sendCachedDownload(const RcloneUrl &url);
    void clearCachedDownload();

    static void configureProcess(QProcess &process, const QString &program, const QStringList &arguments);
    static void stopProcess(QProcess &process);
    static void collectProcessOutput(QProcess &process, QByteArray &standardOutput, QByteArray &standardError);

    RcloneBackend m_backend;
    std::unique_ptr<QTemporaryFile> m_cachedDownload;
    QString m_cachedDownloadSpec;
    QString m_cachedDownloadVersion;
};
