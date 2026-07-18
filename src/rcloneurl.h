/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
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
    [[nodiscard]] QUrl url() const;

    [[nodiscard]] static QUrl rootUrl();
    [[nodiscard]] static QUrl remoteUrl(const QString &remote);

private:
    bool m_valid = false;
    QUrl m_url;
    QString m_remote;
    QString m_remotePath;
};
