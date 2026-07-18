/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "appid.h"
#include "configwindow.h"

#include <KDBusService>
#include <KLocalizedString>
#include <KWindowSystem>

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);

    KLocalizedString::setApplicationDomain("kio6_rclone");

    application.setOrganizationDomain(QStringLiteral("kde.org"));
    application.setApplicationName(QStringLiteral("kio-rclone-config"));
    application.setApplicationDisplayName(i18n("Rclone Remotes"));
    application.setDesktopFileName(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID));

    // Only one configuration window may exist. Further launches activate the
    // existing instance through D-Bus and then terminate. The KIO worker no
    // longer spawns this application (the "Configure Remotes…" entry is a
    // shortcut resolved by UDS_TARGET_URL), so no lock-file or cooldown guard
    // is needed here.
    KDBusService service(KDBusService::Unique);

    ConfigWindow window;

    QObject::connect(&service, &KDBusService::activateRequested, &window, [&window](const QStringList &, const QString &) {
        if (window.isMinimized()) {
            window.showNormal();
        } else {
            window.show();
        }

        // KDBusService exposes the startup/activation token while handling this
        // signal. Passing it to KWindowSystem makes activation work on both
        // Wayland and X11.
        if (auto *windowHandle = window.windowHandle()) {
            KWindowSystem::updateStartupId(windowHandle);
            window.raise();
            KWindowSystem::activateWindow(windowHandle);
        } else {
            window.raise();
            window.activateWindow();
        }
    });

    window.show();

    return application.exec();
}
