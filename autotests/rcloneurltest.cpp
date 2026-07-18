/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneurl.h"

#include <QTest>

class RcloneUrlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesRoot();
    void parsesRemotePath();
    void supportsHostForm();
    void buildsRemoteUrl();
    void recognizesConfigureEntry();
    void treatsConfigureEntryDescendantsAsConfigureEntry();
};

void RcloneUrlTest::parsesRoot()
{
    const RcloneUrl url(QUrl(QStringLiteral("rclone:/")));
    QVERIFY(url.isValid());
    QVERIFY(url.isRoot());
    QVERIFY(url.remoteSpec().isEmpty());
}

void RcloneUrlTest::parsesRemotePath()
{
    const RcloneUrl url(QUrl(QStringLiteral("rclone:/Google%20Drive/My%20Files/report.pdf")));
    QCOMPARE(url.remote(), QStringLiteral("Google Drive"));
    QCOMPARE(url.remotePath(), QStringLiteral("My Files/report.pdf"));
    QCOMPARE(url.remoteSpec(), QStringLiteral("Google Drive:My Files/report.pdf"));
}

void RcloneUrlTest::supportsHostForm()
{
    const RcloneUrl url(QUrl(QStringLiteral("rclone://Photos/2026/image.jpg")));
    QCOMPARE(url.remote(), QStringLiteral("photos"));
    QCOMPARE(url.remotePath(), QStringLiteral("2026/image.jpg"));
}

void RcloneUrlTest::buildsRemoteUrl()
{
    const QUrl url = RcloneUrl::remoteUrl(QStringLiteral("Google Drive"));
    QCOMPARE(url.scheme(), QStringLiteral("rclone"));
    QCOMPARE(url.path(QUrl::FullyDecoded), QStringLiteral("/Google Drive/"));
}

void RcloneUrlTest::recognizesConfigureEntry()
{
    const RcloneUrl url(QUrl(QStringLiteral("rclone:/") + RcloneUrl::ConfigureEntry));
    QVERIFY(url.isValid());
    QVERIFY(url.isConfigureEntry());
    QVERIFY(!url.isRoot());
    QCOMPARE(url.remote(), RcloneUrl::ConfigureEntry);
    QVERIFY(url.remoteSpec().isEmpty());

    // The configure entry occupies a remote slot with an empty path, so
    // isRemoteRoot() also matches it. Every worker handler therefore checks
    // isConfigureEntry() *before* isRemoteRoot(); this locks that ordering
    // requirement so a future reorder cannot silently treat the launcher as a
    // real remote.
    QVERIFY(url.isRemoteRoot());
}

void RcloneUrlTest::treatsConfigureEntryDescendantsAsConfigureEntry()
{
    const RcloneUrl url(QUrl(QStringLiteral("rclone:/") + RcloneUrl::ConfigureEntry + QStringLiteral("/child")));
    QVERIFY(url.isValid());
    QVERIFY(url.isConfigureEntry());
    QVERIFY(url.remoteSpec().isEmpty());
}

QTEST_GUILESS_MAIN(RcloneUrlTest)

#include "rcloneurltest.moc"
