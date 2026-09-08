/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cache/directorysnapshotcache.h"

#include <KIO/ListJob>
#include <KIO/MimetypeJob>
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
} // namespace

class RcloneDirectoryCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void freshListingSkipsRclone();
    void explicitReloadInvalidatesSnapshot();
    void statAndMimetypeReuseListingSnapshot();
    void successfulPutInvalidatesSnapshot();

private:
    [[nodiscard]] KIO::UDSEntryList listDirectory(bool reload = false);
    [[nodiscard]] bool reloadDirectory();
    [[nodiscard]] qint64 listingCount() const;
    [[nodiscard]] QUrl remoteUrl(const QString &name = {}) const;

    QTemporaryDir m_directory;
    QString m_remoteRoot;
    QString m_listingCounter;
};

void RcloneDirectoryCacheTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    const QString pluginRoot = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(pluginRoot);
    qputenv("QT_PLUGIN_PATH", QFile::encodeName(pluginRoot));
    qputenv("KIO_RCLONE_EXECUTABLE", QByteArrayLiteral(FAKE_RCLONE_PATH));

    m_remoteRoot = m_directory.filePath(QStringLiteral("remote-root"));
    m_listingCounter = m_directory.filePath(QStringLiteral("listing-counter"));
    qputenv("KIO_RCLONE_TEST_REMOTE_ROOT", QFile::encodeName(m_remoteRoot));
    qputenv("KIO_RCLONE_TEST_LIST_COUNTER", QFile::encodeName(m_listingCounter));
}

void RcloneDirectoryCacheTest::init()
{
    DirectorySnapshotCache::setPolicy({DirectoryCacheMode::Fresh, 20});
    DirectorySnapshotCache::clearPersistent();

    QDir(m_remoteRoot).removeRecursively();
    QVERIFY(QDir().mkpath(QDir(m_remoteRoot).filePath(QStringLiteral("test"))));
    QVERIFY(writeFile(QDir(m_remoteRoot).filePath(QStringLiteral("test/cached.txt")), QByteArrayLiteral("cached listing data\n")));
    QVERIFY(QFile::remove(m_listingCounter) || !QFileInfo::exists(m_listingCounter));
}

void RcloneDirectoryCacheTest::freshListingSkipsRclone()
{
    const KIO::UDSEntryList firstEntries = listDirectory();
    QVERIFY(!firstEntries.isEmpty());
    const qint64 afterFirstListing = listingCount();
    QVERIFY(afterFirstListing > 0);

    const KIO::UDSEntryList secondEntries = listDirectory();
    QCOMPARE(secondEntries.size(), firstEntries.size());
    QCOMPARE(listingCount(), afterFirstListing);
}

void RcloneDirectoryCacheTest::explicitReloadInvalidatesSnapshot()
{
    static_cast<void>(listDirectory());
    const qint64 afterInitialListing = listingCount();
    QVERIFY(afterInitialListing > 0);

    const QString failureMarker = QDir(m_remoteRoot).filePath(QStringLiteral(".kio-rclone-test-fail-listing"));
    QVERIFY(writeFile(failureMarker, {}));
    const bool reloadSucceeded = reloadDirectory();
    QVERIFY(QFile::remove(failureMarker));
    QVERIFY(!reloadSucceeded);
    const qint64 afterFailedReload = listingCount();
    QVERIFY(afterFailedReload > afterInitialListing);

    // The failed reload must have removed the old snapshot, so the following
    // regular listing needs another real rclone invocation rather than
    // reviving the successful listing from before the reload.
    static_cast<void>(listDirectory());
    QVERIFY(listingCount() > afterFailedReload);
}

void RcloneDirectoryCacheTest::statAndMimetypeReuseListingSnapshot()
{
    static_cast<void>(listDirectory());
    const qint64 afterListing = listingCount();
    QVERIFY(afterListing > 0);

    auto *statJob = KIO::stat(remoteUrl(QStringLiteral("cached.txt")), KIO::StatJob::SourceSide, KIO::StatDefaultDetails, KIO::HideProgressInfo);
    statJob->setUiDelegate(nullptr);
    statJob->setAutoDelete(false);
    QVERIFY2(statJob->exec(), qPrintable(statJob->errorString()));
    QCOMPARE(statJob->statResult().numberValue(KIO::UDSEntry::UDS_SIZE), qint64(QByteArrayLiteral("cached listing data\n").size()));
    delete statJob;
    QCOMPARE(listingCount(), afterListing);

    auto *mimetypeJob = KIO::mimetype(remoteUrl(QStringLiteral("cached.txt")), KIO::HideProgressInfo);
    mimetypeJob->setUiDelegate(nullptr);
    mimetypeJob->setAutoDelete(false);
    QVERIFY2(mimetypeJob->exec(), qPrintable(mimetypeJob->errorString()));
    QVERIFY(!mimetypeJob->mimetype().isEmpty());
    delete mimetypeJob;
    QCOMPARE(listingCount(), afterListing);
}

void RcloneDirectoryCacheTest::successfulPutInvalidatesSnapshot()
{
    static_cast<void>(listDirectory());
    const qint64 afterInitialListing = listingCount();
    QVERIFY(afterInitialListing > 0);

    auto *putJob = KIO::storedPut(QByteArrayLiteral("written through KIO\n"),
                                  remoteUrl(QStringLiteral("new.txt")),
                                  -1,
                                  KIO::Overwrite | KIO::HideProgressInfo);
    putJob->setUiDelegate(nullptr);
    putJob->setAutoDelete(false);
    QVERIFY2(putJob->exec(), qPrintable(putJob->errorString()));
    delete putJob;
    const qint64 afterMutation = listingCount();

    const KIO::UDSEntryList entries = listDirectory();
    QVERIFY(listingCount() > afterMutation);
    const auto added = std::find_if(entries.cbegin(), entries.cend(), [](const KIO::UDSEntry &entry) {
        return entry.stringValue(KIO::UDSEntry::UDS_NAME) == QLatin1String("new.txt");
    });
    QVERIFY(added != entries.cend());
}

KIO::UDSEntryList RcloneDirectoryCacheTest::listDirectory(bool reload)
{
    KIO::UDSEntryList entries;
    auto *job = KIO::listDir(remoteUrl(), KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    if (reload) {
        job->addMetaData(QStringLiteral("cache"), QStringLiteral("reload"));
    }
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

bool RcloneDirectoryCacheTest::reloadDirectory()
{
    auto *job = KIO::listDir(remoteUrl(), KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    job->addMetaData(QStringLiteral("cache"), QStringLiteral("reload"));
    const bool success = job->exec();
    delete job;
    return success;
}

qint64 RcloneDirectoryCacheTest::listingCount() const
{
    QFile counter(m_listingCounter);
    if (!counter.open(QIODevice::ReadOnly)) {
        return 0;
    }
    qint64 count = 0;
    return counter.read(reinterpret_cast<char *>(&count), sizeof(count)) == sizeof(count) ? count : 0;
}

QUrl RcloneDirectoryCacheTest::remoteUrl(const QString &name) const
{
    return QUrl(QStringLiteral("rclone:/test/") + name);
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
    application.setApplicationName(QStringLiteral("rclonedirectorycachetest"));
    RcloneDirectoryCacheTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "rclonedirectorycachetest.moc"
