/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QHash>
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
    bool ambiguous = false;
    bool readOnly = false;
    QDateTime modificationTime;
};

struct RcloneSpace {
    qint64 total = -1;
    qint64 free = -1;
    qint64 used = -1;
};

struct RcloneRemoteInfo {
    QString name;
    QString type;
    bool hasClientId = false;
    bool hasRootFolderId = false;
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
    [[nodiscard]] QHash<QString, QString> remoteTypes(QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneRemoteInfo>
    remoteInfo(const QString &remote, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] QList<RcloneItem> list(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneItem> stat(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneSpace> about(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;

    [[nodiscard]] static QString locateExecutable();
    [[nodiscard]] static QList<RcloneItem> parseItemList(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static std::optional<RcloneItem> parseItem(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static QStringList parseRemoteList(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static QHash<QString, QString> parseRemoteTypes(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static std::optional<RcloneRemoteInfo> parseRemoteInfo(const QByteArray &config, QString *error = nullptr);
    [[nodiscard]] static bool isNotFoundError(const QString &error);

private:
    QString m_executable;
};
