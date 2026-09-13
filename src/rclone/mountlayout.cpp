/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mountlayout.h"

#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace {

constexpr auto kioRcloneDirectory = "kio-rclone";
constexpr auto mountDirectory = "mounts";
constexpr auto cacheDirectory = "vfs";

void appendIdentityField(QByteArray &identity, const QString &field)
{
    const QByteArray encoded = field.toUtf8();
    identity.append(QByteArray::number(encoded.size()));
    identity.append(':');
    identity.append(encoded);
}

bool containsControlCharacter(const QString &value)
{
    for (const QChar character : value) {
        if (character.category() == QChar::Other_Control) {
            return true;
        }
    }
    return false;
}

QString asciiSlug(const QString &value)
{
    const QString normalized = value.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString slug;
    slug.reserve(qMin(normalized.size(), qsizetype(40)));

    for (const QChar character : normalized) {
        const ushort code = character.unicode();
        const bool asciiLetter = (code >= ushort('a') && code <= ushort('z'));
        const bool asciiDigit = (code >= ushort('0') && code <= ushort('9'));

        if (asciiLetter || asciiDigit) {
            if (slug.size() < 40) {
                slug.append(character);
            }
        } else if (character.category() == QChar::Mark_NonSpacing
                   || character.category() == QChar::Mark_SpacingCombining
                   || character.category() == QChar::Mark_Enclosing) {
            // Ignore decomposed accent marks rather than separating a word.
        } else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')) && slug.size() < 40) {
            slug.append(QLatin1Char('-'));
        }
    }

    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }
    return slug;
}

} // namespace

RcloneMountLayout::RcloneMountLayout(QString name,
                                     QString type,
                                     RcloneMountLayoutConfig config)
    : m_name(std::move(name))
    , m_type(std::move(type))
    , m_remotePath(std::move(config.remotePath))
    , m_mountRoot(absoluteRoot(config.mountRoot.isEmpty() ? defaultMountRoot()
                                                          : config.mountRoot))
    , m_cacheRoot(absoluteRoot(config.cacheRoot.isEmpty() ? defaultCacheRoot()
                                                          : config.cacheRoot))
{
    m_valid = !m_name.trimmed().isEmpty()
        && !m_type.trimmed().isEmpty()
        && hasOnlySafeRemoteNameCharacters(m_name)
        && !containsControlCharacter(m_type)
        && isSafeRemotePath(m_remotePath)
        && !m_mountRoot.isEmpty()
        && !m_cacheRoot.isEmpty();

    if (!m_valid) {
        return;
    }

    QByteArray identity;
    appendIdentityField(identity, m_name);
    appendIdentityField(identity, m_type);
    appendIdentityField(identity, m_remotePath);
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());

    m_integrationId = QStringLiteral("kio-rclone-%1-%2-%3")
                          .arg(safeSlug(m_name, QStringLiteral("remote")),
                               safeSlug(m_type, QStringLiteral("unknown")),
                               digest);
}

QString RcloneMountLayout::name() const
{
    return m_name;
}

QString RcloneMountLayout::type() const
{
    return m_type;
}

QString RcloneMountLayout::remotePath() const
{
    return m_remotePath;
}

QString RcloneMountLayout::remoteSpec() const
{
    if (!m_valid) {
        return {};
    }

    QString spec = m_name + QLatin1Char(':');
    if (!m_remotePath.isEmpty()) {
        spec += m_remotePath;
    }
    return spec;
}

QString RcloneMountLayout::integrationId() const
{
    return m_integrationId;
}

QString RcloneMountLayout::mountPath() const
{
    if (!m_valid) {
        return {};
    }
    return QDir(m_mountRoot).filePath(m_integrationId);
}

QString RcloneMountLayout::vfsCachePath() const
{
    if (!m_valid) {
        return {};
    }
    return QDir(m_cacheRoot).filePath(m_integrationId);
}

bool RcloneMountLayout::isValid() const
{
    return m_valid;
}

QString RcloneMountLayout::safeSlug(const QString &value, const QString &fallback)
{
    const QString slug = asciiSlug(value);
    if (slug.isEmpty()) {
        const QString safeFallback = asciiSlug(fallback);
        return safeFallback.isEmpty() ? QStringLiteral("remote") : safeFallback;
    }
    return slug;
}

QString RcloneMountLayout::defaultMountRoot()
{
    const QString runtimeRoot = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeRoot.isEmpty()) {
        return {};
    }
    return QDir(runtimeRoot).filePath(QStringLiteral("%1/%2").arg(QString::fromLatin1(kioRcloneDirectory),
                                                                   QString::fromLatin1(mountDirectory)));
}

QString RcloneMountLayout::defaultCacheRoot()
{
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cacheRoot.isEmpty()) {
        return {};
    }
    return QDir(cacheRoot).filePath(QStringLiteral("%1/%2").arg(QString::fromLatin1(kioRcloneDirectory),
                                                                 QString::fromLatin1(cacheDirectory)));
}

QString RcloneMountLayout::absoluteRoot(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return {};
    }
    return QDir::cleanPath(path);
}

bool RcloneMountLayout::hasOnlySafeRemoteNameCharacters(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char(':'))
        && !containsControlCharacter(name);
}

bool RcloneMountLayout::isSafeRemotePath(const QString &path)
{
    if (path.isEmpty()) {
        return true;
    }
    if (path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\'))
        || containsControlCharacter(path)) {
        return false;
    }

    const QStringList segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QStringLiteral(".") || segment == QStringLiteral("..")) {
            return false;
        }
    }
    return true;
}
