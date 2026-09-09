/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cache/directorylistingpolicy.h"
#include "cache/directorysnapshotcache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class DirectorySnapshotCacheTest : public QObject
{
    Q_OBJECT

public:
    explicit DirectorySnapshotCacheTest(QString rcloneConfigPath)
        : m_rcloneConfigPath(std::move(rcloneConfigPath))
    {
    }

private Q_SLOTS:
    void initTestCase()
    {
        writeRcloneConfig(QByteArrayLiteral("[work]\ntype = local\n"));
    }

    void init()
    {
        DirectoryListingPolicyStore::save({DirectoryListingMode::RecentSnapshot, 20});
        DirectorySnapshotCache::clearPersistent();
    }

    void storesAndLoadsFreshSnapshot()
    {
        DirectorySnapshotCache writer;
        const QList<RcloneItem> expected = {item(QStringLiteral("notes.txt"), 42), item(QStringLiteral("projects"), -1, true)};
        QVERIFY(writer.store(QStringLiteral("work"), QStringLiteral("Documents"), expected));

        DirectorySnapshotCache reader;
        const auto snapshot = reader.load(QStringLiteral("work"), QStringLiteral("Documents"), 20);
        QVERIFY(snapshot);
        QCOMPARE(snapshot->size(), expected.size());
        QCOMPARE(snapshot->at(0).name, QStringLiteral("notes.txt"));
        QCOMPARE(snapshot->at(0).size, 42);
        QCOMPARE(snapshot->at(1).name, QStringLiteral("projects"));
        QVERIFY(snapshot->at(1).isDirectory);
    }

    void preservesSyntheticDirectories()
    {
        RcloneItem synthetic = item(QStringLiteral("path-to-trash"), -1, true);
        synthetic.readOnly = true;
        synthetic.syntheticDirectory = true;

        DirectorySnapshotCache writer;
        QVERIFY(writer.store(QStringLiteral("work"), QStringLiteral("drive/trash"), {synthetic}));

        DirectorySnapshotCache reader;
        const auto snapshot = reader.load(QStringLiteral("work"), QStringLiteral("drive/trash"), 20);
        QVERIFY(snapshot);
        QCOMPARE(snapshot->size(), 1);
        QVERIFY(snapshot->constFirst().syntheticDirectory);
        QVERIFY(snapshot->constFirst().isDirectory);
    }

    void survivesAnotherProcess()
    {
        DirectorySnapshotCache cache;
        QVERIFY(cache.store(QStringLiteral("work"), QStringLiteral("Documents"), {item(QStringLiteral("from-another-process.txt"), 9)}));

        QProcess probe;
        probe.start(QString::fromLocal8Bit(DIRECTORY_SNAPSHOT_CACHE_PROBE),
                    {QStringLiteral("work"), QStringLiteral("Documents"), QStringLiteral("from-another-process.txt")});
        QVERIFY2(probe.waitForStarted(), qPrintable(probe.errorString()));
        QVERIFY2(probe.waitForFinished(), qPrintable(probe.errorString()));
        QCOMPARE(probe.exitStatus(), QProcess::NormalExit);
        QCOMPARE(probe.exitCode(), 0);
    }

    void policyStoreNormalizesValues()
    {
        DirectoryListingPolicyStore::save({DirectoryListingMode::RemoteOnly, 999});

        const DirectoryListingPolicy policy = DirectoryListingPolicyStore::load();
        QCOMPARE(policy.mode, DirectoryListingMode::RemoteOnly);
        QCOMPARE(policy.freshnessSeconds, DirectoryListingPolicy::MaximumFreshnessSeconds);
    }

    void zeroFreshnessNeverUsesSnapshot()
    {
        DirectorySnapshotCache cache;
        QVERIFY(cache.store(QStringLiteral("work"), QStringLiteral("Documents"), {item(QStringLiteral("old.txt"), 1)}));

        QVERIFY(!cache.load(QStringLiteral("work"), QStringLiteral("Documents"), 0));
    }

    void expiredSnapshotIsIgnored()
    {
        DirectoryListingPolicyStore::save({DirectoryListingMode::RecentSnapshot, 1});
        DirectorySnapshotCache cache;
        QVERIFY(cache.store(QStringLiteral("work"), QStringLiteral("Documents"), {item(QStringLiteral("old.txt"), 1)}));

        QTest::qWait(1200);
        QVERIFY(!cache.load(QStringLiteral("work"), QStringLiteral("Documents"), 1));
    }

    void changingRcloneConfigurationInvalidatesSnapshot()
    {
        DirectorySnapshotCache cache;
        QVERIFY(cache.store(QStringLiteral("work"), QStringLiteral("Documents"), {item(QStringLiteral("old.txt"), 1)}));

        writeRcloneConfig(QByteArrayLiteral("[work]\ntype = local\nchanged = true\n"));
        QVERIFY(!cache.load(QStringLiteral("work"), QStringLiteral("Documents"), 20));
    }

    void rejectsUnsafeItems()
    {
        DirectorySnapshotCache cache;
        QVERIFY(!cache.store(QStringLiteral("work"), QStringLiteral("Documents"), {item(QStringLiteral("not/a-file"), 1)}));
    }

    void cacheFileIsPrivate()
    {
        DirectorySnapshotCache cache;
        QVERIFY(cache.store(QStringLiteral("work"), QStringLiteral("Documents"), {item(QStringLiteral("private.txt"), 1)}));

        const QString cacheFile = qEnvironmentVariable("XDG_CACHE_HOME") + QStringLiteral("/kio-rclone/directory-snapshots-v2.kcache");
        const QFileDevice::Permissions permissions = QFile::permissions(cacheFile);
        QVERIFY(permissions & QFileDevice::ReadOwner);
        QVERIFY(permissions & QFileDevice::WriteOwner);
        QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)));
    }

private:
    static RcloneItem item(const QString &name, qint64 size, bool isDirectory = false)
    {
        RcloneItem result;
        result.name = name;
        result.path = name;
        result.mimeType = isDirectory ? QStringLiteral("inode/directory") : QStringLiteral("text/plain");
        result.size = size;
        result.isDirectory = isDirectory;
        return result;
    }

    void writeRcloneConfig(const QByteArray &contents)
    {
        QFile config(m_rcloneConfigPath);
        QVERIFY2(config.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(config.errorString()));
        QCOMPARE(config.write(contents), contents.size());
    }

    QString m_rcloneConfigPath;
};

int main(int argc, char **argv)
{
    QTemporaryDir cacheHome;
    QTemporaryDir configHome;
    if (!cacheHome.isValid() || !configHome.isValid()) {
        return 2;
    }

    qputenv("XDG_CACHE_HOME", cacheHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    const QString rcloneConfigPath = QDir(configHome.path()).filePath(QStringLiteral("rclone.conf"));
    qputenv("RCLONE_CONFIG", rcloneConfigPath.toUtf8());

    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("directorysnapshotcachetest"));
    DirectorySnapshotCacheTest test(rcloneConfigPath);
    return QTest::qExec(&test, argc, argv);
}

#include "directorysnapshotcachetest.moc"
