/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclone/models.h"

#include <KIO/UDSEntry>

#include <QString>

/**
 * KIO-boundary mapping from the project's remote model to KIO metadata.
 *
 * UDS_NAME is a KIO URL path component, not arbitrary remote metadata. Use
 * isRepresentable() before publishing an item received from rclone. See
 * https://api.kde.org/kio-udsentry.html
 */
namespace KioEntryBuilder
{
[[nodiscard]] KIO::UDSEntry root();
[[nodiscard]] KIO::UDSEntry configure();
[[nodiscard]] KIO::UDSEntry remote(const QString &name, bool currentDirectory = false, const QString &type = {});
[[nodiscard]] KIO::UDSEntry directory(const QString &name,
                                       const QString &displayName,
                                       const QString &iconName,
                                       bool writable);
[[nodiscard]] bool isRepresentable(const RcloneItem &item);
[[nodiscard]] KIO::UDSEntry item(const RcloneItem &item);
}
