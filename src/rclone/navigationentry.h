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

struct RcloneNavigationEntry {
    QString name;
    QString mimeType;
    QString iconName;
    QUrl url;

    qint64 size = -1;
    bool isDirectory = true;
    bool readOnly = false;

    QDateTime modificationTime;

    [[nodiscard]] static RcloneNavigationEntry virtualDirectory(const QString &name,
                                                                const QUrl &url,
                                                                const QString &iconName = QStringLiteral("folder"));

    [[nodiscard]] static RcloneNavigationEntry virtualFile(const QString &name,
                                                           const QUrl &url,
                                                           const QString &mimeType,
                                                           const QString &iconName = {});

    [[nodiscard]] static RcloneNavigationEntry fromItem(const RcloneItem &item,
                                                        const QUrl &url);
};
