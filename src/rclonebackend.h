/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

struct RcloneResult {
    bool started = false;
    bool cancelled = false;
    bool timedOut = false;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;

    [[nodiscard]] bool success() const;
    [[nodiscard]] QString errorMessage() const;
};

struct RcloneItem {
    QString name;
    QString path;
    QString id;
    QString mimeType;
    qint64 size = -1;
    bool isDirectory = false;
    QDateTime modificationTime;
};

struct RcloneSpace {
    qint64 total = -1;
    qint64 free = -1;
    qint64 used = -1;
};

class RcloneBackend
{
public:
    using CancellationCallback = std::function<bool()>;

    explicit RcloneBackend(QString executable = {});

    [[nodiscard]] QString executable() const;
    [[nodiscard]] bool isAvailable() const;

    [[nodiscard]] RcloneResult run(const QStringList &arguments, int timeoutMs = 120000, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] QStringList remotes(QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] QList<RcloneItem> list(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneItem> stat(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneSpace> about(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;

    [[nodiscard]] static QString locateExecutable();
    [[nodiscard]] static QList<RcloneItem> parseItemList(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static std::optional<RcloneItem> parseItem(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static QStringList parseRemoteList(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static bool isNotFoundError(const QString &error);

private:
    QString m_executable;
};
