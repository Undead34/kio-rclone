/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cache/directorysnapshotcache.h"

#include <QCoreApplication>

#include <algorithm>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("kio-rclone-cache-probe"));
    if (application.arguments().size() != 4) {
        return 2;
    }

    const QStringList arguments = application.arguments();
    DirectorySnapshotCache cache;
    const auto snapshot = cache.load(arguments.at(1), arguments.at(2), 20);
    if (!snapshot) {
        return 1;
    }

    const auto item = std::find_if(snapshot->cbegin(), snapshot->cend(), [&arguments](const RcloneItem &candidate) {
        return candidate.name == arguments.at(3);
    });
    return item == snapshot->cend() ? 1 : 0;
}
