/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclonebackend.h"

#include <QString>

namespace RcloneEntryFormat
{
// Maps an rclone remote type (the config "type =" value) to a themed places
// icon. Providers that ship a dedicated Breeze icon get it; everything else
// falls back to the generic cloud folder. A theme missing the named icon
// degrades to the plain folder icon (via the inode/directory MIME type), so no
// existence check is needed here.
[[nodiscard]] QString iconForRemoteType(const QString &type);

[[nodiscard]] QString fallbackMimeType(const RcloneItem &item);
[[nodiscard]] QString itemVersion(const RcloneItem &item);
[[nodiscard]] bool preferItem(const RcloneItem &candidate, const RcloneItem &current);
}
