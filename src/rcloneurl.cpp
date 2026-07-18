/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneurl.h"

const QString RcloneUrl::ConfigureEntry = QStringLiteral(".kio-rclone-config");

RcloneUrl::RcloneUrl(const QUrl &url)
{
    if (url.scheme() != QLatin1String("rclone")) {
        return;
    }
    m_url = url;

    QStringList parts;
    if (!url.host().isEmpty()) {
        parts.append(url.host(QUrl::FullyDecoded));
    }

    const QString path = url.path(QUrl::FullyDecoded);
    const auto pathParts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    parts.append(pathParts);

    if (!parts.isEmpty()) {
        m_remote = parts.takeFirst();
        m_remotePath = parts.join(QLatin1Char('/'));
    }
    m_valid = true;
}

bool RcloneUrl::isValid() const
{
    return m_valid;
}

bool RcloneUrl::isRoot() const
{
    return m_valid && m_remote.isEmpty();
}

bool RcloneUrl::isConfigureEntry() const
{
    return m_valid && m_remote == ConfigureEntry;
}

bool RcloneUrl::isRemoteRoot() const
{
    return m_valid && !m_remote.isEmpty() && m_remotePath.isEmpty();
}

QString RcloneUrl::remote() const
{
    return m_remote;
}

QString RcloneUrl::remotePath() const
{
    return m_remotePath;
}

QString RcloneUrl::remoteSpec() const
{
    if (m_remote.isEmpty() || isConfigureEntry()) {
        return {};
    }

    return m_remote + QLatin1Char(':') + m_remotePath;
}

QUrl RcloneUrl::url() const
{
    return m_url;
}

QUrl RcloneUrl::rootUrl()
{
    QUrl url;
    url.setScheme(QStringLiteral("rclone"));
    url.setPath(QStringLiteral("/"));
    return url;
}

QUrl RcloneUrl::remoteUrl(const QString &remote)
{
    QUrl url = rootUrl();
    url.setPath(QLatin1Char('/') + remote + QLatin1Char('/'));
    return url;
}
