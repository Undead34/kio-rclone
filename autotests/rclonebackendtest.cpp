/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rclonebackend.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class RcloneBackendTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesRemoteLists();
    void parsesRemoteInfo();
    void parsesListing();
    void listsLocalRemote();
};

void RcloneBackendTest::parsesRemoteLists()
{
    QString error;
    const QStringList legacy = RcloneBackend::parseRemoteList(R"(["Work:", "Personal:"])", &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(legacy, QStringList({QStringLiteral("Personal"), QStringLiteral("Work")}));

    const QStringList current = RcloneBackend::parseRemoteList(
        R"([
          {"name":"Photos","type":"drive","source":"file"},
          {"name":"Archive","type":"s3","source":"file"}
      ])",
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(current, QStringList({QStringLiteral("Archive"), QStringLiteral("Photos")}));
}

void RcloneBackendTest::parsesRemoteInfo()
{
    QString error;
    const auto sharedClient = RcloneBackend::parseRemoteInfo(
        R"([Google Drive]
type = drive
scope = drive
token = XXX
)",
        &error);
    QVERIFY2(sharedClient.has_value(), qPrintable(error));
    QCOMPARE(sharedClient->name, QStringLiteral("Google Drive"));
    QCOMPARE(sharedClient->type, QStringLiteral("drive"));
    QVERIFY(!sharedClient->hasClientId);
    QVERIFY(!sharedClient->hasRootFolderId);

    const auto privateClient = RcloneBackend::parseRemoteInfo(
        R"([Work]
type = drive
client_id = 123.apps.googleusercontent.com
client_secret = XXX
root_folder_id = root-id
)",
        &error);
    QVERIFY2(privateClient.has_value(), qPrintable(error));
    QVERIFY(privateClient->hasClientId);
    QVERIFY(privateClient->hasRootFolderId);
}

void RcloneBackendTest::parsesListing()
{
    const QByteArray json = R"([
        {
            "Path": "folder",
            "Name": "folder",
            "Size": -1,
            "MimeType": "inode/directory",
            "ModTime": "2026-07-16T10:20:30.123Z",
            "IsDir": true
        },
        {
            "Path": "report.pdf",
            "Name": "report.pdf",
            "Size": 42,
            "MimeType": "application/pdf",
            "ModTime": "2026-07-16T10:20:30Z",
            "IsDir": false,
            "ID": "abc"
        }
    ])";

    QString error;
    const QList<RcloneItem> items = RcloneBackend::parseItemList(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(items.size(), 2);
    QVERIFY(items.at(0).isDirectory);
    QCOMPARE(items.at(1).name, QStringLiteral("report.pdf"));
    QCOMPARE(items.at(1).size, 42);
    QCOMPARE(items.at(1).id, QStringLiteral("abc"));
}

void RcloneBackendTest::listsLocalRemote()
{
    const RcloneBackend backend;
    if (!backend.isAvailable()) {
        QSKIP("rclone is not installed");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("hello.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("hello"), 5);
    file.close();

    QString error;
    const QList<RcloneItem> items = backend.list(QStringLiteral(":local:") + directory.path(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.constFirst().name, QStringLiteral("hello.txt"));
    QCOMPARE(items.constFirst().size, 5);

    const auto item = backend.stat(QStringLiteral(":local:") + file.fileName(), &error);
    QVERIFY2(item.has_value(), qPrintable(error));
    QCOMPARE(item->name, QStringLiteral("hello.txt"));
    QCOMPARE(item->size, 5);
}

QTEST_GUILESS_MAIN(RcloneBackendTest)

#include "rclonebackendtest.moc"
