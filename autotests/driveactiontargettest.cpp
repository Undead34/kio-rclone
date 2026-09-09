/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphin-actions/driveactiontarget.h"
#include "dolphin-actions/googleurls.h"

#include <QUrlQuery>
#include <QtTest>

using namespace DolphinDriveActions;

namespace {

QUrl withIdentifier(QUrl url,
                    const QString &key,
                    const QString &identifier)
{
    QUrlQuery query(url);
    query.addQueryItem(key, identifier);
    url.setQuery(query);
    return url;
}

} // namespace

class DriveActionTargetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsWorkerItemId();
    void fallsBackToOriginalWorkerId();
    void keepsTheParentFreeOfAChildId();
    void mapsSharedDriveRequests();
    void readsFilteredDriveViewIds();
    void recognizesOnlyGoogleDriveRemoteType();
    void rejectsUnsupportedAndMalformedLocations();
    void createsSafeGoogleUrls();
};

void DriveActionTargetTest::readsWorkerItemId()
{
    const QUrl url = withIdentifier(
        RcloneLocation::driveViewUrl(
            QStringLiteral("Google Drive"),
            RcloneLocation::Kind::DriveMyDrive,
            QStringLiteral("University/file.pdf")),
        QStringLiteral("rclone-id"),
        QStringLiteral("1AUQ6JRdv_bUZHQsoMzFrG7_0IAgNQu4w"));

    QString error;
    const auto target = DriveActionTarget::fromUrl(url, &error);

    QVERIFY2(target.has_value(), qPrintable(error));
    QCOMPARE(target->remoteName(), QStringLiteral("Google Drive"));
    QCOMPARE(target->remotePath(), QStringLiteral("University/file.pdf"));
    QCOMPARE(target->itemId(), QStringLiteral("1AUQ6JRdv_bUZHQsoMzFrG7_0IAgNQu4w"));
    QCOMPARE(target->statSpec(), QStringLiteral("Google Drive:University/file.pdf"));
}

void DriveActionTargetTest::fallsBackToOriginalWorkerId()
{
    const QUrl url = withIdentifier(
        RcloneLocation::driveViewUrl(
            QStringLiteral("Personal"),
            RcloneLocation::Kind::DriveMyDrive,
            QStringLiteral("shortcut")),
        QStringLiteral("rclone-orig-id"),
        QStringLiteral("1OriginalDriveId"));

    QString error;
    const auto target = DriveActionTarget::fromUrl(url, &error);

    QVERIFY2(target.has_value(), qPrintable(error));
    QCOMPARE(target->itemId(), QStringLiteral("1OriginalDriveId"));
}

void DriveActionTargetTest::keepsTheParentFreeOfAChildId()
{
    const QUrl url = withIdentifier(
        RcloneLocation::driveViewUrl(
            QStringLiteral("Personal"),
            RcloneLocation::Kind::DriveMyDrive,
            QStringLiteral("Projects/report.pdf")),
        QStringLiteral("rclone-id"),
        QStringLiteral("1FileId"));

    QString error;
    const auto target = DriveActionTarget::fromUrl(url, &error);

    QVERIFY2(target.has_value(), qPrintable(error));
    const DriveActionTarget parent = target->parent();
    QCOMPARE(parent.remotePath(), QStringLiteral("Projects"));
    QVERIFY(parent.itemId().isEmpty());
}

void DriveActionTargetTest::mapsSharedDriveRequests()
{
    const QUrl url = withIdentifier(
        RcloneLocation::driveSharedDriveUrl(
            QStringLiteral("Work"),
            QStringLiteral("0AAbcdEFG123"),
            QStringLiteral("Engineering"),
            QStringLiteral("Plans")),
        QStringLiteral("rclone-id"),
        QStringLiteral("1FolderId"));

    QString error;
    const auto target = DriveActionTarget::fromUrl(url, &error);

    QVERIFY2(target.has_value(), qPrintable(error));
    QVERIFY(target->allowsCreation());
    QCOMPARE(
        target->statSpec(),
        QStringLiteral("Work,team_drive=0AAbcdEFG123,root_folder_id=:Plans"));
    QCOMPARE(
        target->listingArguments(),
        QStringList({QStringLiteral("--drive-team-drive"), QStringLiteral("0AAbcdEFG123")}));
}

void DriveActionTargetTest::readsFilteredDriveViewIds()
{
    const QUrl url = withIdentifier(
        RcloneLocation::driveViewUrl(
            QStringLiteral("Personal"),
            RcloneLocation::Kind::DriveSharedWithMe,
            QStringLiteral("Shared project/brief.odt")),
        QStringLiteral("rclone-id"),
        QStringLiteral("1SharedItemId"));

    QString error;
    const auto target = DriveActionTarget::fromUrl(url, &error);

    QVERIFY2(target.has_value(), qPrintable(error));
    QCOMPARE(target->itemId(), QStringLiteral("1SharedItemId"));
    QVERIFY(!target->isRoot());
    QVERIFY(!target->supportsPathFallback());
    QVERIFY(!target->allowsCreation());
    QCOMPARE(
        target->listingArguments(),
        QStringList({QStringLiteral("--drive-shared-with-me")}));

    const auto filteredRoot = DriveActionTarget::fromUrl(
        RcloneLocation::driveViewUrl(
            QStringLiteral("Personal"),
            RcloneLocation::Kind::DriveSharedWithMe),
        &error);
    QVERIFY2(filteredRoot.has_value(), qPrintable(error));
    QVERIFY(!filteredRoot->isRoot());
    QVERIFY(!filteredRoot->supportsPathFallback());
}

void DriveActionTargetTest::recognizesOnlyGoogleDriveRemoteType()
{
    QUrl driveUrl = RcloneLocation::driveViewUrl(
        QStringLiteral("Google Drive"),
        RcloneLocation::Kind::DriveMyDrive,
        QStringLiteral("University/file.pdf"));
    QUrlQuery query(driveUrl);
    query.addQueryItem(QStringLiteral("rclone-remote-type"), QStringLiteral("drive"));
    driveUrl.setQuery(query);

    QVERIFY(DriveActionTarget::hasGoogleDriveRemoteType(driveUrl));

    QUrl nonDriveUrl = driveUrl;
    query = QUrlQuery(nonDriveUrl);
    query.removeAllQueryItems(QStringLiteral("rclone-remote-type"));
    query.addQueryItem(QStringLiteral("rclone-remote-type"), QStringLiteral("s3"));
    nonDriveUrl.setQuery(query);
    QVERIFY(!DriveActionTarget::hasGoogleDriveRemoteType(nonDriveUrl));

    QUrl ambiguousUrl = driveUrl;
    query = QUrlQuery(ambiguousUrl);
    query.addQueryItem(QStringLiteral("rclone-remote-type"), QStringLiteral("s3"));
    ambiguousUrl.setQuery(query);
    QVERIFY(!DriveActionTarget::hasGoogleDriveRemoteType(ambiguousUrl));
}

void DriveActionTargetTest::rejectsUnsupportedAndMalformedLocations()
{
    QString error;
    const auto standard = DriveActionTarget::fromUrl(
        RcloneLocation::standardUrl(
            QStringLiteral("Dropbox"),
            QStringLiteral("notes.txt")),
        &error);
    QVERIFY(!standard.has_value());
    QVERIFY(!error.isEmpty());

    const QUrl malformed = RcloneLocation::driveSharedDriveUrl(
        QStringLiteral("Work"),
        QStringLiteral("unsafe,option=true"),
        QStringLiteral("Engineering"));
    const auto invalidIdentifier = DriveActionTarget::fromUrl(malformed, &error);
    QVERIFY(!invalidIdentifier.has_value());

    const QUrl invalidItemId = withIdentifier(
        RcloneLocation::driveViewUrl(
            QStringLiteral("Personal"),
            RcloneLocation::Kind::DriveMyDrive,
            QStringLiteral("Report")),
        QStringLiteral("rclone-id"),
        QStringLiteral("unsafe/id"));
    const auto invalidItem = DriveActionTarget::fromUrl(invalidItemId, &error);
    QVERIFY(!invalidItem.has_value());
}

void DriveActionTargetTest::createsSafeGoogleUrls()
{
    QCOMPARE(
        driveOpenUrl(QStringLiteral("1File_Id")),
        QUrl(QStringLiteral("https://drive.google.com/open?id=1File_Id")));
    QCOMPARE(
        driveFolderUrl(QStringLiteral("1Folder_Id")),
        QUrl(QStringLiteral("https://drive.google.com/drive/folders/1Folder_Id")));
    QCOMPARE(
        workspaceCreateUrl(WorkspaceFileType::Spreadsheet, QStringLiteral("1Folder_Id")),
        QUrl(QStringLiteral("https://docs.google.com/spreadsheets/create?usp=drive_web&folder=1Folder_Id")));
    QCOMPARE(
        workspaceCreateUrl(WorkspaceFileType::Document),
        QUrl(QStringLiteral("https://docs.google.com/document/create?usp=drive_web")));
}

QTEST_GUILESS_MAIN(DriveActionTargetTest)

#include "driveactiontargettest.moc"
