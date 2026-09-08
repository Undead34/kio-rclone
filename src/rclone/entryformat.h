/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rcloneclient.h"

#include <QString>

namespace RcloneEntryFormat
{
// Pure presentation and comparison rules for RcloneItem. This namespace keeps
// provider-specific metadata out of command parsing and out of the KIO worker;
// construction of KIO::UDSEntry belongs at the KIO boundary instead.

// Maps an rclone remote type (the config "type =" value) to a themed places
// icon. Providers that ship a dedicated Breeze icon get it; everything else
// falls back to the generic cloud folder. A theme missing the named icon
// degrades to the plain folder icon (via the inode/directory MIME type), so no
// existence check is needed here.
[[nodiscard]] QString iconForRemoteType(const QString &type);

[[nodiscard]] QString fallbackMimeType(const RcloneItem &item);

/// Stable-enough identity used to decide whether a materialized download still
/// matches the remote item. It is not a cryptographic content checksum.
[[nodiscard]] QString itemVersion(const RcloneItem &item);

/// Chooses the deterministic representative shown when a backend exposes two
/// objects with one visible name. Mutating such a representative is forbidden.
[[nodiscard]] bool preferItem(const RcloneItem &candidate, const RcloneItem &current);
}
