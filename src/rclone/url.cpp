/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "url.h"

namespace
{
bool isValidRemoteName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('-')) || name.startsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char(' '))) {
        return false;
    }

    for (const QChar character : name) {
        if (character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-') || character == QLatin1Char('.')
            || character == QLatin1Char('+') || character == QLatin1Char('@') || character == QLatin1Char(' ')) {
            continue;
        }
        return false;
    }
    return true;
}
} // namespace

// ':' is invalid in an rclone remote name, while KIO permits it in a path
// component. Reserving it prevents the virtual launcher from shadowing user
// data such as a remote literally named ".kio-rclone-config".
const QString RcloneUrl::ConfigureEntry = QStringLiteral(".kio-rclone-config:");
const QString RcloneUrl::ConfigurationLauncherScheme = QStringLiteral(KIO_RCLONE_CONFIG_LAUNCH_SCHEME);
const QString RcloneUrl::ConfigurationLauncherMimeType = QStringLiteral(KIO_RCLONE_CONFIG_LAUNCH_MIME_TYPE);

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

    // A KIO URL is hierarchical, while some rclone backends (notably Google
    // Drive) allow literal dot names. They cannot be represented safely here:
    // accepting them would turn a remote object into traversal syntax.
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..") || part.contains(QChar::Null)) {
            return;
        }
    }

    if (!parts.isEmpty()) {
        if (parts.constFirst() != ConfigureEntry && !isValidRemoteName(parts.constFirst())) {
            return;
        }
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

QUrl RcloneUrl::configurationLauncherUrl()
{
    QUrl url;
    url.setScheme(ConfigurationLauncherScheme);
    url.setPath(QStringLiteral("/"));
    return url;
}
