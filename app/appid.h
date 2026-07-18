/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// D-Bus desktop file id, .desktop filename, metainfo id, and installed icon
// name for kio-rclone-config. All four must match exactly for D-Bus
// activation, KDE Discover, and window icon lookup to agree on the same
// application. Keep this the single source of truth in C++ code instead of
// repeating the literal in main.cpp and configwindow.cpp.
#define KIO_RCLONE_CONFIG_APP_ID "org.kde.kio-rclone-config"
