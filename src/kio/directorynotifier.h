/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QList>
#include <QUrl>

/**
 * Narrow KIO-notification boundary for successful remote mutations.
 *
 * Keep KDirNotify calls here so an operation can state its visible effect
 * without knowing the generated DBus interface. Call this only after the
 * relevant directory snapshots have been invalidated. See
 * https://api.kde.org/legacy/4.12-api/kdelibs-apidocs/kio/html/classOrgKdeKDirNotifyInterface.html
 */
namespace KioDirectoryNotifier
{
void filesChanged(const QList<QUrl> &urls);
void filesAdded(const QUrl &parentDirectory);
void filesRemoved(const QList<QUrl> &urls);
void fileRenamed(const QUrl &source, const QUrl &destination);
}
