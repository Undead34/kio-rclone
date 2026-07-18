/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "appid.h"
#include "configwindow.h"

#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>

namespace
{
QString configGuardBasePath()
{
    QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty()) {
        runtimeDir = QDir::tempPath();
    }
    return runtimeDir + QStringLiteral("/kio-rclone-config");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setOrganizationDomain(QStringLiteral("kde.org"));
    application.setApplicationName(QStringLiteral("kio-rclone-config"));
    application.setApplicationDisplayName(i18n("Rclone Remotes"));
    application.setDesktopFileName(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID));

    // Repeated listDir() calls on the "Configure Remotes…" KIO entry (from
    // Dolphin, indexers, or recursive tools walking a kio-fuse mount) each
    // launch this executable again. Without a single-instance guard, every
    // call opened a brand new window instead of reusing the existing one.
    KDBusService service(KDBusService::Unique);

    // Fallback single-instance guard for when the session bus is unreachable
    // (e.g. launched from a kioworker walking a kiofuse mount), where
    // KDBusService::Unique cannot dedupe. The KIO worker also refuses to spawn
    // us while this lock is held; holding it here keeps that check honest.
    const QString guardBase = configGuardBasePath();
    QLockFile instanceLock(guardBase + QStringLiteral(".lock"));
    if (!instanceLock.tryLock(100)) {
        return 0; // another window is already open; do not open a second one
    }

    // Refresh the launch cooldown stamp on exit so a recursive walk still in
    // flight when the user closes the window does not immediately reopen it.
    QObject::connect(&application, &QApplication::aboutToQuit, [guardBase]() {
        QFile stamp(guardBase + QStringLiteral(".launch"));
        if (stamp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            stamp.close();
        }
    });

    ConfigWindow window;
    QObject::connect(&service, &KDBusService::activateRequested, &window, [&window](const QStringList &, const QString &) {
        window.show();
        window.raise();
        window.activateWindow();
    });

    window.show();
    return application.exec();
}
