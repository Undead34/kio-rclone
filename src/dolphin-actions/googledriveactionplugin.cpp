/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "driveactiontarget.h"

#include <KAbstractFileItemActionPlugin>
#include <KFileItem>
#include <KFileItemListProperties>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QAction>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString actionHelperExecutable()
{
    const QString installedPath = QStringLiteral(KIO_RCLONE_DRIVE_ACTION_EXECUTABLE);

    if (QFileInfo(installedPath).isExecutable()) {
        return installedPath;
    }

    return QStandardPaths::findExecutable(QStringLiteral("kio-rclone-drive-action"));
}

} // namespace

class GoogleDriveActionPlugin final : public KAbstractFileItemActionPlugin
{
    Q_OBJECT

public:
    GoogleDriveActionPlugin(QObject *parent, const QVariantList &arguments)
        : KAbstractFileItemActionPlugin(parent)
    {
        Q_UNUSED(arguments)
    }

    [[nodiscard]] QList<QAction *> actions(
        const KFileItemListProperties &fileItemInfos,
        QWidget *parentWidget) override;

private:
    QAction *addHelperAction(QMenu *menu,
                             const QString &text,
                             const QString &icon,
                             QStringList arguments,
                             const QUrl &url);
};

K_PLUGIN_CLASS_WITH_JSON(GoogleDriveActionPlugin, "googledriveactionplugin.json")

QList<QAction *> GoogleDriveActionPlugin::actions(
    const KFileItemListProperties &fileItemInfos,
    QWidget *parentWidget)
{
    const KFileItemList items = fileItemInfos.items();

    if (items.size() != 1) {
        return {};
    }

    const QUrl url = items.constFirst().url();

    // This is intentionally the first and only visibility gate. It is pure
    // URL parsing, so opening a contextual menu never starts rclone or makes a
    // network request.
    if (!DolphinDriveActions::DriveActionTarget::hasGoogleDriveRemoteType(url)) {
        return {};
    }

    QString ignoredError;
    const auto target = DolphinDriveActions::DriveActionTarget::fromUrl(
        url,
        &ignoredError);

    if (!target) {
        return {};
    }

    auto *driveMenu = new QMenu(i18n("Google Drive"), parentWidget);
    driveMenu->setIcon(QIcon::fromTheme(QStringLiteral("folder-gdrive")));
    addHelperAction(
        driveMenu,
        i18n("Open in Google Drive"),
        QStringLiteral("internet-services"),
        {QStringLiteral("open")},
        url);
    addHelperAction(
        driveMenu,
        i18n("Copy Google Drive link"),
        QStringLiteral("edit-copy"),
        {QStringLiteral("copy-link")},
        url);
    addHelperAction(
        driveMenu,
        i18n("Open containing folder in Google Drive"),
        QStringLiteral("folder-open"),
        {QStringLiteral("open-folder")},
        url);

    QList<QAction *> result{driveMenu->menuAction()};

    if (!fileItemInfos.isDirectory() || !target->allowsCreation()) {
        return result;
    }

    auto *newMenu = new QMenu(i18n("New Google Drive files"), parentWidget);
    newMenu->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    addHelperAction(
        newMenu,
        i18n("Google Doc"),
        QStringLiteral("text-editor"),
        {QStringLiteral("new"), QStringLiteral("doc")},
        url);
    addHelperAction(
        newMenu,
        i18n("Google Sheets"),
        QStringLiteral("x-office-spreadsheet"),
        {QStringLiteral("new"), QStringLiteral("sheet")},
        url);
    addHelperAction(
        newMenu,
        i18n("Google Slides"),
        QStringLiteral("x-office-presentation"),
        {QStringLiteral("new"), QStringLiteral("slide")},
        url);
    addHelperAction(
        newMenu,
        i18n("Google Drawings"),
        QStringLiteral("image-x-generic"),
        {QStringLiteral("new"), QStringLiteral("drawing")},
        url);

    result.append(newMenu->menuAction());
    return result;
}

QAction *GoogleDriveActionPlugin::addHelperAction(
    QMenu *menu,
    const QString &text,
    const QString &icon,
    QStringList arguments,
    const QUrl &url)
{
    auto *action = menu->addAction(QIcon::fromTheme(icon), text);
    arguments.append(url.toString(QUrl::FullyEncoded));

    connect(action, &QAction::triggered, this, [this, arguments] {
        const QString executable = actionHelperExecutable();

        if (executable.isEmpty()
            || !QProcess::startDetached(executable, arguments)) {
            Q_EMIT error(i18n("Unable to start the Google Drive action helper."));
        }
    });

    return action;
}

#include "googledriveactionplugin.moc"
