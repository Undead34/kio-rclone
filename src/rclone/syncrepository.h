/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "client.h"

#include <KSharedConfig>

#include <QDateTime>
#include <QString>

#include <optional>

/**
 * @brief Estado persistente de una copia local.
 *
 * Los estados transitorios como SyncingDown y SyncingUp pertenecen a
 * RcloneRemoteFile y no se persisten. Si el proceso termina durante una
 * transferencia, el repositorio conserva únicamente un estado recuperable.
 */
enum class RcloneSyncState {
    CloudOnly,
    Synced,
    Modified,
    Conflict,
};

/**
 * @brief Registro persistente de una copia local sincronizada.
 *
 * Describe dónde está almacenada la copia local, qué estado tiene y contra
 * qué versión del objeto remoto fue sincronizada por última vez.
 */
struct RcloneSyncEntry {
    QString remoteSpec;
    QString localPath;

    RcloneSyncState state = RcloneSyncState::CloudOnly;

    RcloneTargetSnapshot remoteSnapshot;

    QDateTime lastSyncTime;

    [[nodiscard]] bool hasLocalCopy() const
    {
        return !localPath.isEmpty();
    }

    [[nodiscard]] bool isDirty() const
    {
        return state == RcloneSyncState::Modified
            || state == RcloneSyncState::Conflict;
    }
};

/**
 * @brief Repositorio persistente para el estado de sincronización.
 *
 * Utiliza KSharedConfig::openStateConfig() para almacenar únicamente metadata.
 * Los contenidos reales de los archivos permanecen fuera de KConfig.
 *
 * El repositorio no toma decisiones de sincronización. RcloneRemoteFile es
 * responsable de decidir cuándo un archivo pasa a Synced, Modified o Conflict.
 */
class RcloneSyncRepository
{
public:
    RcloneSyncRepository();

    /**
     * @brief Busca el registro correspondiente a un objeto remoto.
     *
     * @return El registro persistido o std::nullopt si nunca se ha
     *         materializado localmente.
     */
    [[nodiscard]] std::optional<RcloneSyncEntry>
    find(const QString &remoteSpec) const;

    /**
     * @brief Guarda o reemplaza el registro de sincronización.
     */
    void save(const RcloneSyncEntry &entry);

    /**
     * @brief Elimina el registro persistente.
     *
     * No elimina el archivo físico indicado por localPath.
     */
    void remove(const QString &remoteSpec);

    /**
     * @brief Comprueba si existe metadata persistida para el objeto.
     */
    [[nodiscard]] bool
    contains(const QString &remoteSpec) const;

private:
    /**
     * @brief Genera un identificador estable y seguro para usar como grupo KConfig.
     */
    [[nodiscard]] static QString
    keyForRemoteSpec(const QString &remoteSpec);

    KSharedConfig::Ptr m_config;
};
