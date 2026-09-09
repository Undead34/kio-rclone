/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rclone/location.h"

#include <QTest>

class RcloneLocationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mapsStandardRemote();
    void exposesDriveHub();
    void mapsDriveViews();
    void rejectsInvalidDrivePaths();
};

void RcloneLocationTest::mapsStandardRemote()
{
    const RcloneLocation location = RcloneLocation::standard(QStringLiteral("Archive"), QStringLiteral("reports/2026"));

    QCOMPARE(location.kind(), RcloneLocation::Kind::Standard);
    QCOMPARE(location.rcloneSpec(), QStringLiteral("Archive:reports/2026"));
    QCOMPARE(location.parentSpec(), QStringLiteral("Archive:reports"));
    QCOMPARE(location.cachePath(), QStringLiteral("reports/2026"));
    QVERIFY(location.canWrite());
}

void RcloneLocationTest::exposesDriveHub()
{
    const auto hub = RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QString());
    QVERIFY(hub.has_value());
    QVERIFY(hub->isDriveHub());
    QVERIFY(hub->isVirtualDirectory());
    QVERIFY(!hub->hasRcloneTarget());

    const QList<RcloneLocation::HubEntry> entries = RcloneLocation::driveHubEntries();
    QCOMPARE(entries.size(), 5);
    QCOMPARE(entries.at(0).name, QStringLiteral("my-drive"));
    QCOMPARE(entries.at(1).name, QStringLiteral("shared-with-me"));
    QCOMPARE(entries.at(2).name, QStringLiteral("shared-drives"));
    QCOMPARE(entries.at(3).name, QStringLiteral("trash"));
    QCOMPARE(entries.at(4).name, QStringLiteral("starred"));
}

void RcloneLocationTest::mapsDriveViews()
{
    const auto myDrive = RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("my-drive/Work/report.odt"));
    QVERIFY(myDrive.has_value());
    QCOMPARE(myDrive->rcloneSpec(), QStringLiteral("Google Drive:Work/report.odt"));
    QCOMPARE(myDrive->parentSpec(), QStringLiteral("Google Drive:Work"));
    QCOMPARE(myDrive->cachePath(), QStringLiteral("drive/my-drive/Work/report.odt"));
    QVERIFY(myDrive->canWrite());

    const auto shared = RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("shared-with-me/Shared/file.txt"));
    QVERIFY(shared.has_value());
    QCOMPARE(shared->rcloneSpec(), QStringLiteral("Google Drive,shared_with_me:Shared/file.txt"));
    QCOMPARE(shared->cachePath(), QStringLiteral("drive/shared-with-me/Shared/file.txt"));
    QVERIFY(shared->cachePath() != myDrive->cachePath());
    QVERIFY(!shared->canWrite());

    const auto sharedDrive = RcloneLocation::googleDrive(
        QStringLiteral("Google Drive"), QStringLiteral("shared-drives/0ABC-def_123/Design/mockup.svg"));
    QVERIFY(sharedDrive.has_value());
    QCOMPARE(sharedDrive->rcloneSpec(), QStringLiteral("Google Drive,team_drive=0ABC-def_123,root_folder_id=:Design/mockup.svg"));
    QCOMPARE(sharedDrive->rcloneRootSpec(), QStringLiteral("Google Drive,team_drive=0ABC-def_123,root_folder_id=:"));
    QCOMPARE(sharedDrive->cachePath(), QStringLiteral("drive/shared-drives/0ABC-def_123/Design/mockup.svg"));
    QVERIFY(sharedDrive->canWrite());

    const auto folder = RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("folders/1Folder_ID/Documents"));
    QVERIFY(folder.has_value());
    QCOMPARE(folder->rcloneSpec(), QStringLiteral("Google Drive,root_folder_id=1Folder_ID:Documents"));
    QCOMPARE(folder->mutationScope(), QStringLiteral("drive:folder:1Folder_ID"));
    QVERIFY(!folder->sharesMutationScope(*myDrive));
    QVERIFY(folder->canWrite());

    const auto trash = RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("trash/old.txt"));
    QVERIFY(trash.has_value());
    QCOMPARE(trash->rcloneSpec(), QStringLiteral("Google Drive,trashed_only:old.txt"));
    QVERIFY(!trash->canWrite());
}

void RcloneLocationTest::rejectsInvalidDrivePaths()
{
    QVERIFY(!RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("Documents")).has_value());
    QVERIFY(!RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("shared-drives/bad,id")).has_value());
    QVERIFY(!RcloneLocation::googleDrive(QStringLiteral("Google Drive"), QStringLiteral("folders/../private")).has_value());
}

QTEST_GUILESS_MAIN(RcloneLocationTest)

#include "rclonelocationtest.moc"
