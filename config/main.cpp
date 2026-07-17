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

    ConfigWindow window;
    QObject::connect(&service, &KDBusService::activateRequested, &window, [&window](const QStringList &, const QString &) {
        window.show();
        window.raise();
        window.activateWindow();
    });

    window.show();
    return application.exec();
}
