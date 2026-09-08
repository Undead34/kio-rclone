/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QUrl>

/**
 * Canonical parser for the public rclone:/ URL space.
 *
 * This class is the transport boundary between QUrl/KIO and rclone path
 * strings. Code outside that boundary should not rebuild remote specs by
 * slicing QUrl paths independently.
 */
class RcloneUrl
{
public:
    static const QString ConfigureEntry;
    static const QString ConfigurationLauncherScheme;
    static const QString ConfigurationLauncherMimeType;

    explicit RcloneUrl(const QUrl &url);

    /// A valid URL is a safe, canonical representation of an rclone root,
    /// remote root, remote path, or the virtual configuration launcher.
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isRoot() const;
    [[nodiscard]] bool isConfigureEntry() const;
    [[nodiscard]] bool isRemoteRoot() const;
    [[nodiscard]] QString remote() const;
    [[nodiscard]] QString remotePath() const;
    /// rclone's `remote:path` form for backend commands. It is not a display
    /// URL and must not be shown to users as a substitute for url().
    [[nodiscard]] QString remoteSpec() const;
    [[nodiscard]] QUrl url() const;

    [[nodiscard]] static QUrl rootUrl();
    [[nodiscard]] static QUrl remoteUrl(const QString &remote);
    [[nodiscard]] static QUrl configurationLauncherUrl();

private:
    bool m_valid = false;
    QUrl m_url;
    QString m_remote;
    QString m_remotePath;
};
