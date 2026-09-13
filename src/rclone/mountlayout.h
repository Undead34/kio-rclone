/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

/**
 * @brief Configuration for the local paths and remote folder of one mount.
 *
 * Empty local roots use the per-session runtime directory for the mount point
 * and the user's cache directory for rclone's VFS cache. Empty remotePath
 * mounts the configured remote's root.
 */
struct RcloneMountLayoutConfig {
    QString remotePath;
    QString mountRoot;
    QString cacheRoot;
};

/**
 * @brief Stable identity and local paths for mounting one configured rclone remote.
 *
 * The rclone remote name is kept verbatim for remote specs. A separate,
 * filesystem-safe ID is generated for local paths; its SHA-256 suffix prevents
 * names that slugify identically from sharing a mount or cache directory.
 * This class only describes paths and never creates directories or starts a
 * mount process.
 */
class RcloneMountLayout
{
public:
    RcloneMountLayout(QString name,
                      QString type,
                      RcloneMountLayoutConfig config = {});

    [[nodiscard]] QString name() const;
    [[nodiscard]] QString type() const;
    [[nodiscard]] QString remotePath() const;
    [[nodiscard]] QString remoteSpec() const;
    [[nodiscard]] QString integrationId() const;
    [[nodiscard]] QString mountPath() const;
    [[nodiscard]] QString vfsCachePath() const;
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Make a readable lowercase ASCII component from a display string.
     *
     * Diacritics are folded where Unicode decomposition permits it; other
     * punctuation and whitespace become a single dash. The result is capped
     * at 40 characters and uses fallback when no ASCII letters or digits remain.
     */
    [[nodiscard]] static QString safeSlug(const QString &value,
                                          const QString &fallback);

private:
    static QString defaultMountRoot();
    static QString defaultCacheRoot();
    static QString absoluteRoot(const QString &path);
    static bool hasOnlySafeRemoteNameCharacters(const QString &name);
    static bool isSafeRemotePath(const QString &path);

    QString m_name;
    QString m_type;
    QString m_remotePath;
    QString m_mountRoot;
    QString m_cacheRoot;
    QString m_integrationId;
    bool m_valid = false;
};
