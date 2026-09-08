/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QList>
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

struct RcloneRemote {
    QString name;
    QString type;
};

class RcloneClient
{
public:
    using CancellationCallback = std::function<bool()>;
    using ItemCallback = std::function<bool(const RcloneItem &)>;

    explicit RcloneClient(QString executable = {});

    [[nodiscard]] QString executable() const;
    [[nodiscard]] bool isAvailable() const;

    [[nodiscard]] RcloneResult run(const QStringList &arguments, int timeoutMs = 120000, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] QList<RcloneRemote> remoteList(QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] QStringList remotes(QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneRemoteInfo>
    remoteInfo(const QString &remote, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    /// Returns whether rclone declares that this remote can expose duplicate
    /// names. An indeterminate result must be treated as potentially unsafe.
    [[nodiscard]] std::optional<bool>
    mayHaveDuplicateNames(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    /// Calls \a onItem while rclone is still producing lsjson output. Returning
    /// false from the callback stops the child process and aborts the listing.
    [[nodiscard]] bool listStreaming(const QString &remoteSpec,
                                     const ItemCallback &onItem,
                                     QString *error = nullptr,
                                     const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] QList<RcloneItem> list(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneItem> stat(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;
    [[nodiscard]] std::optional<RcloneSpace> about(const QString &remoteSpec, QString *error = nullptr, const CancellationCallback &isCancelled = {}) const;

    [[nodiscard]] static QString locateExecutable();
    [[nodiscard]] static QList<RcloneItem> parseItemList(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static std::optional<RcloneItem> parseItem(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static QList<RcloneRemote> parseRemoteListWithTypes(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static QStringList parseRemoteList(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static std::optional<bool> parseDuplicateNameSupport(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] static std::optional<RcloneRemoteInfo> parseRemoteInfo(const QByteArray &config, QString *error = nullptr);
    [[nodiscard]] static bool isNotFoundError(const QString &error);

private:
    QString m_executable;
};
