/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "configwindow.h"

#include <KLocalizedString>

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("kio-rclone-config"));
    application.setApplicationDisplayName(i18n("Rclone Remotes"));
    application.setDesktopFileName(QStringLiteral("org.kde.kio-rclone-config"));

    ConfigWindow window;
    window.show();
    return application.exec();
}
