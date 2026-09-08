/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rclone/rcloneclient.h"

#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

class RcloneClientTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesRemoteLists();
    void parsesDuplicateNameSupport();
    void parsesRemoteInfo();
    void parsesListing();
    void listsLocalRemote();
    void streamsFragmentedListing();
};

void RcloneClientTest::parsesRemoteLists()
{
    QString error;
    const QStringList legacy = RcloneClient::parseRemoteList(R"(["Work:", "Personal:"])", &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(legacy, QStringList({QStringLiteral("Personal"), QStringLiteral("Work")}));

    const QStringList current = RcloneClient::parseRemoteList(
        R"([
          {"name":"Photos","type":"drive","source":"file"},
          {"name":"Archive","type":"s3","source":"file"}
      ])",
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(current, QStringList({QStringLiteral("Archive"), QStringLiteral("Photos")}));

    const QList<RcloneRemote> details = RcloneClient::parseRemoteListWithTypes(
        R"([
          {"name":"Photos","type":"drive","source":"file"},
          {"name":"Archive","type":"s3","source":"file"}
      ])",
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(details.size(), 2);
    QCOMPARE(details.constFirst().name, QStringLiteral("Archive"));
    QCOMPARE(details.constFirst().type, QStringLiteral("s3"));
    QCOMPARE(details.constLast().name, QStringLiteral("Photos"));
    QCOMPARE(details.constLast().type, QStringLiteral("drive"));
}

void RcloneClientTest::parsesDuplicateNameSupport()
{
    QString error;
    const auto ordinary = RcloneClient::parseDuplicateNameSupport(
        R"({"Features":{"DuplicateFiles":false,"MergeDirs":false}})",
        &error);
    QVERIFY2(ordinary.has_value(), qPrintable(error));
    QVERIFY(!*ordinary);

    const auto drive = RcloneClient::parseDuplicateNameSupport(
        R"({"Features":{"DuplicateFiles":true,"MergeDirs":true}})",
        &error);
    QVERIFY2(drive.has_value(), qPrintable(error));
    QVERIFY(*drive);

    const auto directoryOnly = RcloneClient::parseDuplicateNameSupport(
        R"({"Features":{"DuplicateFiles":false,"MergeDirs":true}})",
        &error);
    QVERIFY2(directoryOnly.has_value(), qPrintable(error));
    QVERIFY(*directoryOnly);

    QString missingError;
    const auto missing = RcloneClient::parseDuplicateNameSupport(R"({"Features":{}})", &missingError);
    QVERIFY(!missing.has_value());
    QVERIFY(!missingError.isEmpty());
}

void RcloneClientTest::parsesRemoteInfo()
{
    QString error;
    const auto sharedClient = RcloneClient::parseRemoteInfo(
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

    const auto privateClient = RcloneClient::parseRemoteInfo(
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

void RcloneClientTest::parsesListing()
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
    const QList<RcloneItem> items = RcloneClient::parseItemList(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(items.size(), 2);
    QVERIFY(items.at(0).isDirectory);
    QCOMPARE(items.at(1).name, QStringLiteral("report.pdf"));
    QCOMPARE(items.at(1).size, 42);
    QCOMPARE(items.at(1).id, QStringLiteral("abc"));
}

void RcloneClientTest::listsLocalRemote()
{
    const RcloneClient backend;
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

    const auto duplicateNames = backend.mayHaveDuplicateNames(QStringLiteral(":local:") + directory.path(), &error);
    QVERIFY2(duplicateNames.has_value(), qPrintable(error));
    QVERIFY(!*duplicateNames);

    QStringList streamedNames;
    QVERIFY2(backend.listStreaming(
                 QStringLiteral(":local:") + directory.path(),
                 [&streamedNames](const RcloneItem &listedItem) {
                     streamedNames.append(listedItem.name);
                     return true;
                 },
                 &error),
             qPrintable(error));
    QCOMPARE(streamedNames, QStringList({QStringLiteral("hello.txt")}));

    const auto item = backend.stat(QStringLiteral(":local:") + file.fileName(), &error);
    QVERIFY2(item.has_value(), qPrintable(error));
    QCOMPARE(item->name, QStringLiteral("hello.txt"));
    QCOMPARE(item->size, 5);
}

void RcloneClientTest::streamsFragmentedListing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString remoteRoot = directory.filePath(QStringLiteral("remote-root"));
    const QString remoteDirectory = QDir(remoteRoot).filePath(QStringLiteral("test"));
    QVERIFY(QDir().mkpath(remoteDirectory));
    QVERIFY(QFile(remoteDirectory + QStringLiteral("/first.txt")).open(QIODevice::WriteOnly));
    QVERIFY(QFile(remoteDirectory + QStringLiteral("/second.txt")).open(QIODevice::WriteOnly));

    qputenv("KIO_RCLONE_TEST_REMOTE_ROOT", QFile::encodeName(remoteRoot));
    qputenv("KIO_RCLONE_TEST_LIST_CHUNK_SIZE", QByteArrayLiteral("32"));
    qputenv("KIO_RCLONE_TEST_LIST_CHUNK_DELAY_MS", QByteArrayLiteral("80"));

    RcloneClient backend(QStringLiteral(FAKE_RCLONE_PATH));
    QString error;
    QStringList names;
    QElapsedTimer timer;
    timer.start();
    qint64 firstItemAt = -1;
    const bool listed = backend.listStreaming(
        QStringLiteral("test:"),
        [&names, &firstItemAt, &timer](const RcloneItem &item) {
            if (firstItemAt < 0) {
                firstItemAt = timer.elapsed();
            }
            names.append(item.name);
            return true;
        },
        &error);
    const qint64 completedAt = timer.elapsed();

    qunsetenv("KIO_RCLONE_TEST_REMOTE_ROOT");
    qunsetenv("KIO_RCLONE_TEST_LIST_CHUNK_SIZE");
    qunsetenv("KIO_RCLONE_TEST_LIST_CHUNK_DELAY_MS");

    QVERIFY2(listed, qPrintable(error));
    QCOMPARE(names, QStringList({QStringLiteral("first.txt"), QStringLiteral("second.txt")}));
    QVERIFY(firstItemAt >= 0);
    QVERIFY2(completedAt - firstItemAt >= 80,
             qPrintable(QStringLiteral("First item arrived only %1 ms before the listing completed").arg(completedAt - firstItemAt)));
}

QTEST_GUILESS_MAIN(RcloneClientTest)

#include "rcloneclienttest.moc"
