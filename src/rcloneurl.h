/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QUrl>

class RcloneUrl
{
public:
    static const QString ConfigureEntry;

    explicit RcloneUrl(const QUrl &url);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isRoot() const;
    [[nodiscard]] bool isConfigureEntry() const;
    [[nodiscard]] bool isRemoteRoot() const;
    [[nodiscard]] QString remote() const;
    [[nodiscard]] QString remotePath() const;
    [[nodiscard]] QString remoteSpec() const;

    [[nodiscard]] static QUrl rootUrl();
    [[nodiscard]] static QUrl remoteUrl(const QString &remote);

private:
    bool m_valid = false;
    QString m_remote;
    QString m_remotePath;
};
