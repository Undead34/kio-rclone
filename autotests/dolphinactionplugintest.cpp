/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphin-actions/driveactiontarget.h"

#include <KAbstractFileItemActionPlugin>
#include <KFileItem>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QUrlQuery>
#include <QWidget>
#include <QtTest>

#include <memory>
#include <sys/stat.h>

using namespace DolphinDriveActions;

namespace {

QUrl driveUrl(const QString &path,
              const QString &remoteType = QStringLiteral("drive"))
{
    QUrl url = RcloneLocation::driveViewUrl(
        QStringLiteral("Google Drive"),
        RcloneLocation::Kind::DriveMyDrive,
        path);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("rclone-id"), QStringLiteral("1DriveItemId"));

    if (!remoteType.isNull()) {
        query.addQueryItem(QStringLiteral("rclone-remote-type"), remoteType);
    }

    url.setQuery(query);
    return url;
}

std::unique_ptr<KAbstractFileItemActionPlugin> loadPlugin()
{
    const KPluginMetaData metadata(
        QStringLiteral(KIO_RCLONE_ACTION_PLUGIN_BINARY));

    if (!metadata.isValid()) {
        return nullptr;
    }

    const auto result = KPluginFactory::instantiatePlugin<KAbstractFileItemActionPlugin>(metadata);
    return std::unique_ptr<KAbstractFileItemActionPlugin>(result.plugin);
}

QList<QAction *> actionsFor(KAbstractFileItemActionPlugin &plugin,
                            const QUrl &url,
                            bool isDirectory,
                            QWidget *parent)
{
    const mode_t mode = isDirectory ? S_IFDIR : S_IFREG;
    const KFileItem item(
        url,
        isDirectory ? QStringLiteral("inode/directory") : QStringLiteral("application/octet-stream"),
        mode);
    const KFileItemListProperties properties(KFileItemList{item});

    return plugin.actions(properties, parent);
}

QAction *actionWithText(const QList<QAction *> &actions, const QString &text)
{
    for (QAction *action : actions) {
        if (action->text() == text) {
            return action;
        }

        if (QMenu *submenu = action->menu()) {
            if (QAction *match = actionWithText(submenu->actions(), text)) {
                return match;
            }
        }
    }

    return nullptr;
}

QAction *actionFromKioMenu(const QUrl &url,
                           const QString &mimeType,
                           bool isDirectory,
                           QWidget *parent)
{
    const mode_t mode = isDirectory ? S_IFDIR : S_IFREG;
    const KFileItem item(url, mimeType, mode);
    const KFileItemListProperties properties(KFileItemList{item});
    QMenu menu(parent);
    KFileItemActions actions(&menu);
    actions.setItemListProperties(properties);
    actions.setParentWidget(parent);
    actions.addActionsTo(&menu, KFileItemActions::MenuActionSource::Plugins);

    QAction *result = actionWithText(menu.actions(), QStringLiteral("Google Drive"));

    if (result) {
        result->setParent(parent);
    }

    return result;
}

} // namespace

class DolphinActionPluginTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void loadsTheNativePlugin();
    void hidesMenusOutsideGoogleDrive();
    void exposesOnlyExpectedDriveMenus();
    void filtersThroughKioFileItemActions();
};

void DolphinActionPluginTest::initTestCase()
{
    QDir pluginDirectory(
        QFileInfo(QStringLiteral(KIO_RCLONE_ACTION_PLUGIN_BINARY)).absolutePath());
    QVERIFY(pluginDirectory.cdUp());
    QVERIFY(pluginDirectory.cdUp());
    QCoreApplication::addLibraryPath(pluginDirectory.absolutePath());

    const KPluginMetaData discovered = KPluginMetaData::findPluginById(
        QStringLiteral("kf6/kfileitemaction"),
        QStringLiteral("kio_rclone_google_drive_action"));
    QVERIFY(discovered.isValid());
}

void DolphinActionPluginTest::loadsTheNativePlugin()
{
    const KPluginMetaData metadata(
        QStringLiteral(KIO_RCLONE_ACTION_PLUGIN_BINARY));

    QVERIFY(metadata.isValid());
    QCOMPARE(metadata.pluginId(), QStringLiteral("kio_rclone_google_drive_action"));
    QVERIFY(metadata.mimeTypes().contains(QStringLiteral("application/octet-stream")));
    QVERIFY(metadata.mimeTypes().contains(QStringLiteral("inode/directory")));

    const auto plugin = loadPlugin();
    QVERIFY(plugin);
}

void DolphinActionPluginTest::hidesMenusOutsideGoogleDrive()
{
    const auto plugin = loadPlugin();
    QVERIFY(plugin);
    QWidget parent;

    QCOMPARE(
        actionsFor(*plugin, driveUrl(QStringLiteral("report.pdf"), QStringLiteral("s3")), false, &parent).size(),
        0);
    QCOMPARE(
        actionsFor(*plugin, driveUrl(QStringLiteral("report.pdf"), QString{}), false, &parent).size(),
        0);
}

void DolphinActionPluginTest::exposesOnlyExpectedDriveMenus()
{
    const auto plugin = loadPlugin();
    QVERIFY(plugin);
    QWidget parent;

    const QList<QAction *> fileActions = actionsFor(
        *plugin,
        driveUrl(QStringLiteral("University/file.pdf")),
        false,
        &parent);
    QCOMPARE(fileActions.size(), 1);
    QCOMPARE(fileActions.constFirst()->text(), QStringLiteral("Google Drive"));
    QVERIFY(fileActions.constFirst()->menu());
    QCOMPARE(fileActions.constFirst()->menu()->actions().size(), 3);

    const QList<QAction *> directoryActions = actionsFor(
        *plugin,
        driveUrl(QStringLiteral("University")),
        true,
        &parent);
    QCOMPARE(directoryActions.size(), 2);
    QCOMPARE(directoryActions.at(1)->text(), QStringLiteral("New Google Drive files"));
    QVERIFY(directoryActions.at(1)->menu());
    QCOMPARE(directoryActions.at(1)->menu()->actions().size(), 4);
}

void DolphinActionPluginTest::filtersThroughKioFileItemActions()
{
    QWidget parent;

    // This path goes through the same KFileItemActions discovery path used by
    // Dolphin, including MIME filtering of installed action plugins.
    QAction *driveAction = actionFromKioMenu(
        driveUrl(QStringLiteral("University/file.pdf")),
        QStringLiteral("application/pdf"),
        false,
        &parent);
    QVERIFY(driveAction);
    QVERIFY(driveAction->menu());
    QCOMPARE(driveAction->menu()->actions().size(), 3);

    QAction *nonDriveAction = actionFromKioMenu(
        driveUrl(QStringLiteral("report.pdf"), QStringLiteral("s3")),
        QStringLiteral("application/pdf"),
        false,
        &parent);
    QVERIFY(!nonDriveAction);

    QAction *directoryAction = actionFromKioMenu(
        driveUrl(QStringLiteral("University")),
        QStringLiteral("inode/directory"),
        true,
        &parent);
    QVERIFY(directoryAction);

    // The top-level Drive menu is found; use direct plugin coverage above to
    // validate the folder-only Workspace submenu composition.
}

QTEST_MAIN(DolphinActionPluginTest)

#include "dolphinactionplugintest.moc"
