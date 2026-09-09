/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "url.h"
#include <QStringList>

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

const QString RcloneUrl::ConfigureEntry = QStringLiteral(".kio-rclone-config:");
const QString RcloneUrl::ConfigurationLauncherScheme = QStringLiteral(KIO_RCLONE_CONFIG_LAUNCH_SCHEME);
const QString RcloneUrl::ConfigurationLauncherMimeType = QStringLiteral(KIO_RCLONE_CONFIG_LAUNCH_MIME_TYPE);

RcloneUrl::RcloneUrl(const QUrl &url, const QString &remote, const QString &remotePath)
    : m_url(url)
    , m_remote(remote)
    , m_remotePath(remotePath)
{
}

std::optional<RcloneUrl> RcloneUrl::parse(const QUrl &url)
{
    if (url.scheme() != QLatin1String("rclone")) {
        return std::nullopt;
    }

    QStringList parts;
    if (!url.host().isEmpty()) {
        parts.append(url.host(QUrl::FullyDecoded));
    }

    const QString path = url.path(QUrl::FullyDecoded);
    const auto pathParts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    parts.append(pathParts);

    // KIO procesa rutas jerárquicas, pero backends como Google Drive permiten
    // nombres literales con puntos. Rechazamos esto para prevenir path traversal.
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..") || part.contains(QChar::Null)) {
            return std::nullopt;
        }
    }

    QString remote;
    QString remotePath;

    if (!parts.isEmpty()) {
        if (parts.constFirst() != ConfigureEntry && !isValidRemoteName(parts.constFirst())) {
            return std::nullopt;
        }
        remote = parts.takeFirst();
        remotePath = parts.join(QLatin1Char('/'));
    }

    return RcloneUrl(url, remote, remotePath);
}

bool RcloneUrl::isRoot() const
{
    return m_remote.isEmpty();
}

bool RcloneUrl::isConfigureEntry() const
{
    return m_remote == ConfigureEntry;
}

bool RcloneUrl::isRemoteRoot() const
{
    return !m_remote.isEmpty() && m_remotePath.isEmpty();
}

QString RcloneUrl::remoteName() const
{
    return m_remote;
}

QString RcloneUrl::remotePath() const
{
    return m_remotePath;
}

QString RcloneUrl::toCliSpec() const
{
    if (m_remote.isEmpty() || isConfigureEntry()) {
        return {};
    }
    return m_remote + QLatin1Char(':') + m_remotePath;
}

QUrl RcloneUrl::toQUrl() const
{
    return m_url;
}

namespace RcloneUrlBuilder
{
QUrl createRoot()
{
    QUrl url;
    url.setScheme(QStringLiteral("rclone"));
    url.setPath(QStringLiteral("/"));
    return url;
}

QUrl createForRemote(const QString &remoteName)
{
    QUrl url = createRoot();
    url.setPath(QLatin1Char('/') + remoteName + QLatin1Char('/'));
    return url;
}

QUrl createConfigLauncher()
{
    QUrl url;
    url.setScheme(RcloneUrl::ConfigurationLauncherScheme);
    url.setPath(QStringLiteral("/"));
    return url;
}
} // namespace RcloneUrlBuilder
