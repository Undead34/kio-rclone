/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclone/rcloneclient.h"

#include <KIO/UDSEntry>

#include <QString>

/**
 * KIO-boundary mapping from the project's remote model to KIO metadata.
 *
 * Callers must validate a remote item's name before passing it to item():
 * UDS_NAME is a KIO URL path component, not arbitrary remote metadata. See
 * https://api.kde.org/kio-udsentry.html
 */
namespace KioEntryBuilder
{
[[nodiscard]] KIO::UDSEntry root();
[[nodiscard]] KIO::UDSEntry configure();
[[nodiscard]] KIO::UDSEntry remote(const QString &name, bool currentDirectory = false, const QString &type = {});
[[nodiscard]] KIO::UDSEntry item(const RcloneItem &item);
}
