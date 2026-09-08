/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "directorysnapshotcache.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedDataCache>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimeZone>

namespace
{
constexpr auto CacheName = "kio-rclone/directory-snapshots-v1";
constexpr unsigned CacheSchemaVersion = 1;
constexpr unsigned CacheSizeBytes = 8U * 1024U * 1024U;
constexpr unsigned ExpectedSnapshotSize = 8U * 1024U;
constexpr quint32 SnapshotMagic = 0x4b524453; // "KRDS"
constexpr quint16 SnapshotVersion = 1;
constexpr qsizetype MaximumSnapshotBytes = 1024 * 1024;
constexpr qint64 MaximumFutureClockSkewMs = 5 * 60 * 1000;

DirectoryCachePolicy normalizedPolicy(DirectoryCachePolicy value)
{
    value.freshnessSeconds = qBound(DirectorySnapshotCache::MinimumFreshnessSeconds,
                                    value.freshnessSeconds,
                                    DirectorySnapshotCache::MaximumFreshnessSeconds);
    return value;
}

QString cacheDirectoryPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/kio-rclone");
}

QString cacheFilePath()
{
    return cacheDirectoryPath() + QStringLiteral("/directory-snapshots-v1.kcache");
}

void ensurePrivateCacheDirectory()
{
    const QString directory = cacheDirectoryPath();
    QDir().mkpath(directory);
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

std::unique_ptr<KSharedDataCache> createSharedCache()
{
    // Restrict the parent before KSharedDataCache creates its mapped file. A
    // conservative umask normally does this too, but this cache must never
    // rely on the caller's process-wide umask for privacy.
    ensurePrivateCacheDirectory();
    auto cache = std::make_unique<KSharedDataCache>(QString::fromLatin1(CacheName), CacheSizeBytes, ExpectedSnapshotSize);
    QFile::setPermissions(cacheFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return cache;
}

QString rcloneConfigurationPath()
{
    const QByteArray configuredPath = qgetenv("RCLONE_CONFIG");
    if (!configuredPath.isEmpty()) {
        return QString::fromLocal8Bit(configuredPath);
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/rclone/rclone.conf");
}

QByteArray configurationIdentity()
{
    const QFileInfo configFile(rcloneConfigurationPath());
    QString identity = configFile.absoluteFilePath();
    if (configFile.exists() && configFile.isFile()) {
        const QString canonicalPath = configFile.canonicalFilePath();
        if (!canonicalPath.isEmpty()) {
            identity = canonicalPath;
        }
        identity += QLatin1Char('\n') + QString::number(configFile.size());
        identity += QLatin1Char('\n') + QString::number(configFile.lastModified().toUTC().toMSecsSinceEpoch());
    } else {
        identity += QStringLiteral("\nmissing");
    }
    return QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256);
}

std::optional<QString> canonicalDirectoryPath(const QString &path)
{
    if (path.contains(QChar::Null)) {
        return std::nullopt;
    }
    if (path.isEmpty()) {
        return QString();
    }

    const QString normalized = QDir::cleanPath(path);
    if (normalized.isEmpty() || normalized == QLatin1String(".") || normalized == QLatin1String("..") || normalized.startsWith(QLatin1String("../"))
        || normalized.startsWith(QLatin1Char('/'))) {
        return std::nullopt;
    }
    return normalized;
}

bool isCacheableItem(const RcloneItem &item)
{
    return !item.name.isEmpty() && !item.name.contains(QLatin1Char('/')) && item.name != QLatin1String(".") && item.name != QLatin1String("..")
        && !item.name.contains(QChar::Null) && (item.path.isEmpty() || item.path == item.name) && item.size >= -1;
}

QString cacheKey(const QString &remote, const QString &remotePath, const QByteArray &configIdentity)
{
    QByteArray material("directory-snapshot-v1\0", 22);
    material.append(remote.toUtf8());
    material.append('\0');
    material.append(remotePath.toUtf8());
    material.append('\0');
    material.append(configIdentity);
    return QStringLiteral("directory-") + QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

QByteArray encodeSnapshot(const QString &remote, const QString &remotePath, const QByteArray &configIdentity, const QList<RcloneItem> &items)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);

    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << SnapshotMagic << SnapshotVersion << remote << remotePath << configIdentity << QDateTime::currentMSecsSinceEpoch()
           << quint32(items.size());
    for (const RcloneItem &item : items) {
        const qint64 modificationTime = item.modificationTime.isValid() ? item.modificationTime.toUTC().toMSecsSinceEpoch() : -1;
        stream << item.name << item.id << item.mimeType << item.size << item.isDirectory << item.ambiguous << item.readOnly << modificationTime;
    }

    if (stream.status() != QDataStream::Ok || encoded.size() > MaximumSnapshotBytes) {
        return {};
    }
    return encoded;
}

std::optional<QList<RcloneItem>> decodeSnapshot(const QByteArray &encoded,
                                                const QString &expectedRemote,
                                                const QString &expectedRemotePath,
                                                const QByteArray &expectedConfigIdentity,
                                                int freshnessSeconds)
{
    if (encoded.isEmpty() || encoded.size() > MaximumSnapshotBytes) {
        return std::nullopt;
    }

    QBuffer buffer;
    buffer.setData(encoded);
    buffer.open(QIODevice::ReadOnly);
    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    quint16 version = 0;
    QString remote;
    QString remotePath;
    QByteArray configIdentity;
    qint64 fetchedAt = -1;
    quint32 itemCount = 0;
    stream >> magic >> version >> remote >> remotePath >> configIdentity >> fetchedAt >> itemCount;
    if (stream.status() != QDataStream::Ok || magic != SnapshotMagic || version != SnapshotVersion || remote != expectedRemote
        || remotePath != expectedRemotePath || configIdentity != expectedConfigIdentity || itemCount > DirectorySnapshotCache::MaximumItemCount) {
        return std::nullopt;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 freshnessMs = qint64(freshnessSeconds) * 1000;
    if (fetchedAt <= 0 || fetchedAt > now + MaximumFutureClockSkewMs || now - fetchedAt > freshnessMs) {
        return std::nullopt;
    }

    QList<RcloneItem> items;
    items.reserve(itemCount);
    for (quint32 index = 0; index < itemCount; ++index) {
        RcloneItem item;
        qint64 modificationTime = -1;
        stream >> item.name >> item.id >> item.mimeType >> item.size >> item.isDirectory >> item.ambiguous >> item.readOnly >> modificationTime;
        item.path = item.name;
        if (stream.status() != QDataStream::Ok || !isCacheableItem(item)) {
            return std::nullopt;
        }
        if (modificationTime >= 0) {
            item.modificationTime = QDateTime::fromMSecsSinceEpoch(modificationTime, QTimeZone::UTC);
        }
        items.append(std::move(item));
    }

    if (stream.status() != QDataStream::Ok || !buffer.atEnd()) {
        return std::nullopt;
    }
    return items;
}

KConfigGroup cachePolicyGroup(KConfig &config)
{
    return KConfigGroup(&config, QStringLiteral("DirectoryListingCache"));
}
} // namespace

DirectorySnapshotCache::DirectorySnapshotCache()
    : m_cache(createSharedCache())
{
    if (m_cache->timestamp() != CacheSchemaVersion) {
        m_cache->clear();
        m_cache->setTimestamp(CacheSchemaVersion);
    }
    m_cache->setEvictionPolicy(KSharedDataCache::EvictLeastRecentlyUsed);
}

DirectorySnapshotCache::~DirectorySnapshotCache() = default;

std::optional<QList<RcloneItem>> DirectorySnapshotCache::load(const QString &remote, const QString &remotePath)
{
    const DirectoryCachePolicy currentPolicy = policy();
    if (!currentPolicy.allowsSnapshots() || remote.isEmpty()) {
        return std::nullopt;
    }

    const std::optional<QString> canonicalPath = canonicalDirectoryPath(remotePath);
    if (!canonicalPath) {
        return std::nullopt;
    }
    const QByteArray configIdentity = configurationIdentity();
    QByteArray encoded;
    if (!m_cache->find(cacheKey(remote, *canonicalPath, configIdentity), &encoded)) {
        return std::nullopt;
    }

    return decodeSnapshot(encoded, remote, *canonicalPath, configIdentity, currentPolicy.freshnessSeconds);
}

bool DirectorySnapshotCache::store(const QString &remote, const QString &remotePath, const QList<RcloneItem> &items)
{
    if (!policy().allowsSnapshots() || remote.isEmpty() || items.size() > MaximumItemCount) {
        return false;
    }

    const std::optional<QString> canonicalPath = canonicalDirectoryPath(remotePath);
    if (!canonicalPath) {
        return false;
    }
    for (const RcloneItem &item : items) {
        if (!isCacheableItem(item)) {
            return false;
        }
    }

    const QByteArray configIdentity = configurationIdentity();
    const QByteArray encoded = encodeSnapshot(remote, *canonicalPath, configIdentity, items);
    return !encoded.isEmpty() && m_cache->insert(cacheKey(remote, *canonicalPath, configIdentity), encoded);
}

void DirectorySnapshotCache::clear()
{
    m_cache->clear();
}

DirectoryCachePolicy DirectorySnapshotCache::policy()
{
    KConfig config(QStringLiteral("kiorclonerc"), KConfig::NoGlobals);
    const KConfigGroup group = cachePolicyGroup(config);
    DirectoryCachePolicy value;
    value.mode = group.readEntry(QStringLiteral("Mode"), QStringLiteral("fresh")) == QLatin1String("strict") ? DirectoryCacheMode::Strict
                                                                                                                   : DirectoryCacheMode::Fresh;
    value.freshnessSeconds = group.readEntry(QStringLiteral("FreshnessSeconds"), DefaultFreshnessSeconds);
    return normalizedPolicy(value);
}

void DirectorySnapshotCache::setPolicy(const DirectoryCachePolicy &value)
{
    const DirectoryCachePolicy normalized = normalizedPolicy(value);
    KConfig config(QStringLiteral("kiorclonerc"), KConfig::NoGlobals);
    KConfigGroup group = cachePolicyGroup(config);
    group.writeEntry(QStringLiteral("Mode"), normalized.mode == DirectoryCacheMode::Strict ? QStringLiteral("strict") : QStringLiteral("fresh"));
    group.writeEntry(QStringLiteral("FreshnessSeconds"), normalized.freshnessSeconds);
    group.sync();

    // A policy change must take effect across workers immediately. Clearing
    // is inexpensive and avoids an old fresh window becoming visible after a
    // user switches through the strict mode.
    clearPersistent();
}

void DirectorySnapshotCache::clearPersistent()
{
    DirectorySnapshotCache cache;
    cache.clear();
}
