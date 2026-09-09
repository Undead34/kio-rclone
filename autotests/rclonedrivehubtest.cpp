/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cache/directorylistingpolicy.h"
#include "cache/directorysnapshotcache.h"

#include <KIO/ListJob>
#include <KIO/StatJob>
#include <KIO/StoredTransferJob>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace
{
bool writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size() && file.flush();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

const KIO::UDSEntry *entryNamed(const KIO::UDSEntryList &entries, QLatin1StringView name)
{
    const auto entry = std::find_if(entries.cbegin(), entries.cend(), [name](const KIO::UDSEntry &candidate) {
        return candidate.stringValue(KIO::UDSEntry::UDS_NAME) == name;
    });
    return entry == entries.cend() ? nullptr : &*entry;
}
} // namespace

class RcloneDriveHubTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void hubListsViewsAndMapsMyDrive();
    void sharedDrivesUseStableIdsAndConnectionStrings();
    void knownFolderIdsMapToDedicatedRoots();
    void filteredViewsAreReadOnly();
    void myDriveAllowsWrites();

private:
    [[nodiscard]] KIO::UDSEntryList listDirectory(const QString &path) const;
    [[nodiscard]] QUrl url(const QString &path = {}) const;
    [[nodiscard]] QByteArray capturedListingSpec() const;

    QTemporaryDir m_directory;
    QString m_remoteRoot;
    QString m_listingSpecPath;
};

void RcloneDriveHubTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    const QString pluginRoot = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(pluginRoot);
    qputenv("QT_PLUGIN_PATH", QFile::encodeName(pluginRoot));
    qputenv("KIO_RCLONE_EXECUTABLE", QByteArrayLiteral(FAKE_RCLONE_PATH));
    qputenv("KIO_RCLONE_TEST_REMOTE_TYPE", QByteArrayLiteral("drive"));

    m_remoteRoot = m_directory.filePath(QStringLiteral("remote-root"));
    m_listingSpecPath = m_directory.filePath(QStringLiteral("listing-spec"));
    qputenv("KIO_RCLONE_TEST_REMOTE_ROOT", QFile::encodeName(m_remoteRoot));
    qputenv("KIO_RCLONE_TEST_LISTING_SPEC", QFile::encodeName(m_listingSpecPath));
}

void RcloneDriveHubTest::init()
{
    DirectoryListingPolicyStore::save({DirectoryListingMode::RemoteOnly, 0});
    DirectorySnapshotCache::clearPersistent();

    QDir(m_remoteRoot).removeRecursively();
    QVERIFY(QDir().mkpath(QDir(m_remoteRoot).filePath(QStringLiteral("test"))));
    QVERIFY(writeFile(QDir(m_remoteRoot).filePath(QStringLiteral("test/hello.txt")), QByteArrayLiteral("hello from Drive\n")));
    QVERIFY(QFile::remove(m_listingSpecPath) || !QFileInfo::exists(m_listingSpecPath));
}

void RcloneDriveHubTest::hubListsViewsAndMapsMyDrive()
{
    auto *hubStat = KIO::stat(url(), KIO::StatJob::SourceSide, KIO::StatDefaultDetails, KIO::HideProgressInfo);
    hubStat->setUiDelegate(nullptr);
    hubStat->setAutoDelete(false);
    QVERIFY2(hubStat->exec(), qPrintable(hubStat->errorString()));
    QCOMPARE(hubStat->statResult().stringValue(KIO::UDSEntry::UDS_NAME), QStringLiteral("test"));
    delete hubStat;

    const KIO::UDSEntryList hub = listDirectory(QString());
    const KIO::UDSEntry *myDrive = entryNamed(hub, QLatin1String("my-drive"));
    QVERIFY(myDrive);
    QVERIFY(!myDrive->stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME).isEmpty());
    QVERIFY(entryNamed(hub, QLatin1String("shared-with-me")));
    QVERIFY(entryNamed(hub, QLatin1String("shared-drives")));
    const KIO::UDSEntry *trash = entryNamed(hub, QLatin1String("trash"));
    QVERIFY(trash);
    QCOMPARE(trash->stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME), QStringLiteral("Trash (original folder structure)"));
    QVERIFY(trash->stringValue(KIO::UDSEntry::UDS_COMMENT).contains(QStringLiteral("original folder structure")));
    QVERIFY(entryNamed(hub, QLatin1String("starred")));

    const KIO::UDSEntryList myDriveEntries = listDirectory(QStringLiteral("my-drive/"));
    QVERIFY(entryNamed(myDriveEntries, QLatin1String("hello.txt")));
    QCOMPARE(capturedListingSpec(), QByteArrayLiteral("test:"));

    auto *myDriveStat = KIO::stat(url(QStringLiteral("my-drive/")),
                                   KIO::StatJob::SourceSide,
                                   KIO::StatDefaultDetails,
                                   KIO::HideProgressInfo);
    myDriveStat->setUiDelegate(nullptr);
    myDriveStat->setAutoDelete(false);
    QVERIFY2(myDriveStat->exec(), qPrintable(myDriveStat->errorString()));
    QCOMPARE(myDriveStat->statResult().stringValue(KIO::UDSEntry::UDS_NAME), QStringLiteral("my-drive"));
    delete myDriveStat;

    static_cast<void>(listDirectory(QStringLiteral("shared-with-me/")));
    QCOMPARE(capturedListingSpec(), QByteArrayLiteral("test,shared_with_me:"));

    static_cast<void>(listDirectory(QStringLiteral("trash/")));
    QCOMPARE(capturedListingSpec(), QByteArrayLiteral("test,trashed_only:"));
}

void RcloneDriveHubTest::sharedDrivesUseStableIdsAndConnectionStrings()
{
    const KIO::UDSEntryList drives = listDirectory(QStringLiteral("shared-drives/"));
    const KIO::UDSEntry *teamFiles = entryNamed(drives, QLatin1String("0A-team"));
    QVERIFY(teamFiles);
    QCOMPARE(teamFiles->stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME), QStringLiteral("Team Files"));

    const KIO::UDSEntryList teamEntries = listDirectory(QStringLiteral("shared-drives/0A-team/"));
    QVERIFY(entryNamed(teamEntries, QLatin1String("hello.txt")));
    QCOMPARE(capturedListingSpec(), QByteArrayLiteral("test,team_drive=0A-team,root_folder_id=:"));
}

void RcloneDriveHubTest::knownFolderIdsMapToDedicatedRoots()
{
    const KIO::UDSEntryList folderEntries = listDirectory(QStringLiteral("folders/1Folder_ID/"));
    QVERIFY(entryNamed(folderEntries, QLatin1String("hello.txt")));
    QCOMPARE(capturedListingSpec(), QByteArrayLiteral("test,root_folder_id=1Folder_ID:"));
}

void RcloneDriveHubTest::filteredViewsAreReadOnly()
{
    auto *job = KIO::storedPut(QByteArrayLiteral("must not be uploaded"),
                               url(QStringLiteral("trash/blocked.txt")),
                               -1,
                               KIO::Overwrite | KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    QVERIFY(!job->exec());
    QCOMPARE(job->error(), int(KIO::ERR_WRITE_ACCESS_DENIED));
    delete job;

    QVERIFY(!QFileInfo::exists(QDir(m_remoteRoot).filePath(QStringLiteral("test/blocked.txt"))));
}

void RcloneDriveHubTest::myDriveAllowsWrites()
{
    const QByteArray contents("written through the Drive hub\n");
    auto *job = KIO::storedPut(contents,
                               url(QStringLiteral("my-drive/new.txt")),
                               -1,
                               KIO::Overwrite | KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    QVERIFY2(job->exec(), qPrintable(job->errorString()));
    delete job;

    QCOMPARE(readFile(QDir(m_remoteRoot).filePath(QStringLiteral("test/new.txt"))), contents);
}

KIO::UDSEntryList RcloneDriveHubTest::listDirectory(const QString &path) const
{
    KIO::UDSEntryList entries;
    auto *job = KIO::listDir(url(path), KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    connect(job, &KIO::ListJob::entries, this, [&entries](KIO::Job *, const KIO::UDSEntryList &batch) {
        entries.append(batch);
    });
    if (!job->exec()) {
        const QString error = job->errorString();
        delete job;
        QTest::qFail(qPrintable(error), __FILE__, __LINE__);
        return {};
    }
    delete job;
    return entries;
}

QUrl RcloneDriveHubTest::url(const QString &path) const
{
    return QUrl(QStringLiteral("rclone:/test/") + path);
}

QByteArray RcloneDriveHubTest::capturedListingSpec() const
{
    return readFile(m_listingSpecPath);
}

int main(int argc, char **argv)
{
    QTemporaryDir cacheHome;
    QTemporaryDir configHome;
    if (!cacheHome.isValid() || !configHome.isValid()) {
        return 2;
    }
    qputenv("XDG_CACHE_HOME", cacheHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("rclonedrivehubtest"));
    RcloneDriveHubTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "rclonedrivehubtest.moc"
