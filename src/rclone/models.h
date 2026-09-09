/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>
#include <QJsonObject>
#include <optional>

/**
 * @brief Representación de un archivo o directorio remoto.
 *
 * Los campos desconocidos conservan sus valores por defecto.
 * Los campos de navegación local pueden ser completados por una capa
 * superior sin modificar la identidad original del elemento.
 */
struct RcloneItem {
    QString name;
    QString path;

    QString id;
    QString originalId;

    QString mimeType;
    QString tier;

    qint64 size = -1;

    bool isDirectory = false;
    bool isBucket = false;

    bool ambiguous = false;
    bool readOnly = false;

    QDateTime modificationTime;

    [[nodiscard]] static std::optional<RcloneItem>
    fromJson(const QJsonObject &object);
};

/**
 * @brief Métricas de almacenamiento de un remoto.
 *
 * -1 indica que el backend no proporcionó esa métrica.
 * Las métricas se expresan en bytes.
 */
struct RcloneSpace {
    qint64 total = -1;
    qint64 free = -1;
    qint64 used = -1;
    qint64 trashed = -1;
    qint64 other = -1;

    [[nodiscard]] static std::optional<RcloneSpace>
    fromJson(const QJsonObject &object);
};

/**
 * @brief Metadatos básicos de un remoto configurado.
 */
struct RcloneRemote {
    QString name;
    QString type;

    [[nodiscard]] static std::optional<RcloneRemote>
    fromJson(const QJsonObject &object);
};
