/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "syncrepository.h"

#include <KConfigGroup>

#include <QCryptographicHash>

namespace {

QString syncStateToString(RcloneSyncState state)
{
    switch (state) {
    case RcloneSyncState::CloudOnly:
        return QStringLiteral("cloud-only");

    case RcloneSyncState::Synced:
        return QStringLiteral("synced");

    case RcloneSyncState::Modified:
        return QStringLiteral("modified");

    case RcloneSyncState::Conflict:
        return QStringLiteral("conflict");
    }

    return QStringLiteral("cloud-only");
}

RcloneSyncState syncStateFromString(const QString &value)
{
    if (value == QLatin1String("synced")) {
        return RcloneSyncState::Synced;
    }

    if (value == QLatin1String("modified")) {
        return RcloneSyncState::Modified;
    }

    if (value == QLatin1String("conflict")) {
        return RcloneSyncState::Conflict;
    }

    return RcloneSyncState::CloudOnly;
}

QString serializeDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return {};
    }

    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime deserializeDateTime(const QString &value)
{
    if (value.isEmpty()) {
        return {};
    }

    return QDateTime::fromString(
        value,
        Qt::ISODateWithMs);
}

} // namespace

RcloneSyncRepository::RcloneSyncRepository()
    : m_config(
          KSharedConfig::openStateConfig(
              QStringLiteral("kio-rclonestaterc")))
{
}

std::optional<RcloneSyncEntry>
RcloneSyncRepository::find(const QString &remoteSpec) const
{
    if (remoteSpec.isEmpty()) {
        return std::nullopt;
    }

    const QString key =
        keyForRemoteSpec(remoteSpec);

    if (!m_config->hasGroup(key)) {
        return std::nullopt;
    }

    const KConfigGroup group(
        m_config,
        key);

    /*
     * Aunque una colisión SHA-256 es prácticamente irrelevante para este caso,
     * verificar el RemoteSpec almacenado también protege contra corrupción o
     * modificaciones manuales del archivo de estado.
     */
    const QString storedRemoteSpec =
        group.readEntry(
            QStringLiteral("RemoteSpec"),
            QString());

    if (storedRemoteSpec != remoteSpec) {
        return std::nullopt;
    }

    RcloneSyncEntry entry;

    entry.remoteSpec = storedRemoteSpec;

    entry.localPath =
        group.readPathEntry(
            QStringLiteral("LocalPath"),
            QString());

    entry.state =
        syncStateFromString(
            group.readEntry(
                QStringLiteral("State"),
                QStringLiteral("cloud-only")));

    entry.remoteSnapshot.exists =
        group.readEntry(
            QStringLiteral("RemoteExists"),
            false);

    entry.remoteSnapshot.isDirectory =
        group.readEntry(
            QStringLiteral("RemoteIsDirectory"),
            false);

    entry.remoteSnapshot.id =
        group.readEntry(
            QStringLiteral("RemoteId"),
            QString());

    entry.remoteSnapshot.size =
        group.readEntry<qint64>(
            QStringLiteral("RemoteSize"),
            -1);

    entry.remoteSnapshot.modificationTime =
        deserializeDateTime(
            group.readEntry(
                QStringLiteral("RemoteModificationTime"),
                QString()));

    entry.lastSyncTime =
        deserializeDateTime(
            group.readEntry(
                QStringLiteral("LastSyncTime"),
                QString()));

    return entry;
}

void RcloneSyncRepository::save(
    const RcloneSyncEntry &entry)
{
    if (entry.remoteSpec.isEmpty()) {
        return;
    }

    const QString key =
        keyForRemoteSpec(entry.remoteSpec);

    KConfigGroup group(
        m_config,
        key);

    group.writeEntry(
        QStringLiteral("RemoteSpec"),
        entry.remoteSpec);

    group.writePathEntry(
        QStringLiteral("LocalPath"),
        entry.localPath);

    group.writeEntry(
        QStringLiteral("State"),
        syncStateToString(entry.state));

    group.writeEntry(
        QStringLiteral("RemoteExists"),
        entry.remoteSnapshot.exists);

    group.writeEntry(
        QStringLiteral("RemoteIsDirectory"),
        entry.remoteSnapshot.isDirectory);

    group.writeEntry(
        QStringLiteral("RemoteId"),
        entry.remoteSnapshot.id);

    group.writeEntry(
        QStringLiteral("RemoteSize"),
        entry.remoteSnapshot.size);

    group.writeEntry(
        QStringLiteral("RemoteModificationTime"),
        serializeDateTime(
            entry.remoteSnapshot.modificationTime));

    group.writeEntry(
        QStringLiteral("LastSyncTime"),
        serializeDateTime(
            entry.lastSyncTime));

    m_config->sync();
}

void RcloneSyncRepository::remove(
    const QString &remoteSpec)
{
    if (remoteSpec.isEmpty()) {
        return;
    }

    const QString key =
        keyForRemoteSpec(remoteSpec);

    if (!m_config->hasGroup(key)) {
        return;
    }

    m_config->deleteGroup(key);
    m_config->sync();
}

bool RcloneSyncRepository::contains(
    const QString &remoteSpec) const
{
    if (remoteSpec.isEmpty()) {
        return false;
    }

    const auto entry = find(remoteSpec);

    return entry.has_value();
}

QString RcloneSyncRepository::keyForRemoteSpec(
    const QString &remoteSpec)
{
    const QByteArray digest =
        QCryptographicHash::hash(
            remoteSpec.toUtf8(),
            QCryptographicHash::Sha256);

    return QString::fromLatin1(
        digest.toHex());
}
