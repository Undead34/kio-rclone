/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kiodirectorynotifier.h"

#include <KDirNotify>

namespace KioDirectoryNotifier
{
void filesChanged(const QList<QUrl> &urls)
{
    OrgKdeKDirNotifyInterface::emitFilesChanged(urls);
}

void filesAdded(const QUrl &parentDirectory)
{
    OrgKdeKDirNotifyInterface::emitFilesAdded(parentDirectory);
}

void filesRemoved(const QList<QUrl> &urls)
{
    OrgKdeKDirNotifyInterface::emitFilesRemoved(urls);
}

void fileRenamed(const QUrl &source, const QUrl &destination)
{
    OrgKdeKDirNotifyInterface::emitFileRenamed(source, destination);
}
}
