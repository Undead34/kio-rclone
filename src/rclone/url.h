/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QUrl>
#include <optional>

/**
 * @brief Parser canónico y seguro para el espacio de URLs rclone:/.
 *
 * Actúa como frontera inmutable entre el ecosistema KIO (QUrl) y el backend de rclone.
 * El diseño garantiza que cualquier instancia viva de esta clase es sintácticamente
 * válida y está protegida contra ataques de path traversal.
 */
class RcloneUrl
{
public:
    static const QString ConfigureEntry;
    static const QString ConfigurationLauncherScheme;
    static const QString ConfigurationLauncherMimeType;

    /// Único punto de instanciación. Valida el esquema, los caracteres permitidos
    /// en el remoto y bloquea rutas con '.' o '..' literales.
    /// @return Un objeto inmutable listo para usar, o nullopt si la URL es inválida/insegura.
    static std::optional<RcloneUrl> parse(const QUrl &url);

    /// @return true si es la raíz absoluta del KIO slave (`rclone:/`).
    [[nodiscard]] bool isRoot() const;

    /// @return true si la URL invoca el módulo virtual de configuración.
    [[nodiscard]] bool isConfigureEntry() const;

    /// @return true si apunta a la raíz de un remoto (`rclone:/mi-remoto/`) sin subrutas.
    [[nodiscard]] bool isRemoteRoot() const;

    [[nodiscard]] QString remoteName() const;
    [[nodiscard]] QString remotePath() const;

    /// Formatea la ruta nativa que exige el binario de rclone (ej. `remoto:ruta/archivo`).
    /// @warning Estrictamente para el backend. No mostrar en la interfaz de usuario.
    [[nodiscard]] QString toCliSpec() const;

    [[nodiscard]] QUrl toQUrl() const;

private:
    explicit RcloneUrl(const QUrl &url, const QString &remote, const QString &path);

    QUrl m_url;
    QString m_remote;
    QString m_remotePath;
};

/**
 * @brief Fábrica pura para la generación de URLs estándar del KIO slave.
 */
namespace RcloneUrlBuilder
{
    [[nodiscard]] QUrl createRoot();
    [[nodiscard]] QUrl createForRemote(const QString &remoteName);
    [[nodiscard]] QUrl createConfigLauncher();
}
