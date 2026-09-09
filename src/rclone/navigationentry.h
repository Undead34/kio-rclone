/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "models.h"

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <optional>

struct RcloneNavigationEntry {
    QString name;
    QString displayName;
    QString mimeType;
    QString iconName;
    QUrl url;
    QUrl targetUrl;

    QString id;
    QString originalId;

    qint64 size = -1;
    bool isDirectory = true;
    bool readOnly = false;
    std::optional<bool> hidden;

    QDateTime modificationTime;

    [[nodiscard]] static RcloneNavigationEntry virtualDirectory(
        const QString &name,
        const QUrl &url,
        const QString &iconName = QStringLiteral("folder"));

    [[nodiscard]] static RcloneNavigationEntry virtualFile(
        const QString &name,
        const QUrl &targetUrl,
        const QString &mimeType,
        const QString &iconName = {},
        const QString &displayName = {});

    [[nodiscard]] static RcloneNavigationEntry fromItem(
        const RcloneItem &item,
        const QUrl &url);
};
