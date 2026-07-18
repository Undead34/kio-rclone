/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KIO/ListJob>
#include <KIO/StatJob>
#include <KIO/StoredTransferJob>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <iterator>
#include <sys/stat.h>

namespace
{
constexpr auto UnknownName = "unknown-size.bin";
constexpr auto DuplicateName = "duplicate.bin";

bool writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size() && file.flush();
}

QJsonObject logicalItem(const QString &name, const QString &id, qint64 size, const QString &modificationTime, const QString &sourcePath)
{
    return {
        {QStringLiteral("Remote"), QStringLiteral("test")},
        {QStringLiteral("Path"), name},
        {QStringLiteral("Name"), name},
        {QStringLiteral("ID"), id},
        {QStringLiteral("Size"), size},
        {QStringLiteral("MimeType"), QStringLiteral("application/octet-stream")},
        {QStringLiteral("ModTime"), modificationTime},
        {QStringLiteral("IsDir"), false},
        {QStringLiteral("Source"), sourcePath},
    };
}
} // namespace

class RcloneDownloadTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void unknownSizeStatMaterializesExactData();
    void unknownSizeIsReadOnly();
    void duplicateNamesCollapseAndDownloadSelectedId();
    void duplicateNamesAreReadOnly();

private:
    [[nodiscard]] QUrl remoteUrl(QLatin1StringView name) const;
    [[nodiscard]] QByteArray storedGet(QLatin1StringView name);

    QTemporaryDir m_directory;
    QString m_remoteRoot;
    QByteArray m_unknownPayload;
    QByteArray m_olderPayload;
    QByteArray m_selectedPayload;
};

void RcloneDownloadTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    const QString pluginRoot = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(pluginRoot);
    qputenv("QT_PLUGIN_PATH", QFile::encodeName(pluginRoot));
    qputenv("KIO_RCLONE_EXECUTABLE", QByteArrayLiteral(FAKE_RCLONE_PATH));
    qunsetenv("KIO_RCLONE_TEST_TRANSFER_COUNTER");

    m_remoteRoot = m_directory.filePath(QStringLiteral("remote-root"));
    QVERIFY(QDir().mkpath(QDir(m_remoteRoot).filePath(QStringLiteral("test"))));
    qputenv("KIO_RCLONE_TEST_REMOTE_ROOT", QFile::encodeName(m_remoteRoot));

    m_unknownPayload = QByteArrayLiteral("unknown-size payload with embedded byte: ");
    m_unknownPayload.append('\0');
    m_unknownPayload.append(QByteArray(256 * 1024 + 73, 'U'));
    m_olderPayload = QByteArrayLiteral("older duplicate that must never be concatenated");
    m_selectedPayload = QByteArrayLiteral("newer duplicate selected by modification time");

    const QString objectsDirectory = m_directory.filePath(QStringLiteral("objects"));
    const QString unknownPath = QDir(objectsDirectory).filePath(QStringLiteral("unknown.data"));
    const QString olderPath = QDir(objectsDirectory).filePath(QStringLiteral("duplicate-older.data"));
    const QString selectedPath = QDir(objectsDirectory).filePath(QStringLiteral("duplicate-selected.data"));
    QVERIFY(writeFile(unknownPath, m_unknownPayload));
    QVERIFY(writeFile(olderPath, m_olderPayload));
    QVERIFY(writeFile(selectedPath, m_selectedPayload));

    const QJsonArray items{
        logicalItem(QLatin1String(UnknownName), QStringLiteral("unknown-id"), -1, QStringLiteral("2026-07-14T10:00:00.000Z"), unknownPath),
        logicalItem(QLatin1String(DuplicateName),
                    QStringLiteral("duplicate-older-id"),
                    m_olderPayload.size(),
                    QStringLiteral("2026-07-14T10:00:00.000Z"),
                    olderPath),
        logicalItem(QLatin1String(DuplicateName),
                    QStringLiteral("duplicate-selected-id"),
                    m_selectedPayload.size(),
                    QStringLiteral("2026-07-15T10:00:00.000Z"),
                    selectedPath),
    };
    const QString manifestPath = m_directory.filePath(QStringLiteral("logical-items.json"));
    QVERIFY(writeFile(manifestPath, QJsonDocument(items).toJson(QJsonDocument::Compact)));
    qputenv("KIO_RCLONE_TEST_LOGICAL_ITEMS", QFile::encodeName(manifestPath));
}

void RcloneDownloadTest::unknownSizeStatMaterializesExactData()
{
    auto *statJob = KIO::stat(remoteUrl(QLatin1String(UnknownName)), KIO::StatJob::SourceSide, KIO::StatDefaultDetails, KIO::HideProgressInfo);
    statJob->setUiDelegate(nullptr);
    statJob->setAutoDelete(false);
    QVERIFY2(statJob->exec(), qPrintable(statJob->errorString()));
    QCOMPARE(statJob->statResult().numberValue(KIO::UDSEntry::UDS_SIZE, -1), m_unknownPayload.size());
    QVERIFY(!(statJob->statResult().numberValue(KIO::UDSEntry::UDS_ACCESS) & S_IWUSR));
    delete statJob;

    QCOMPARE(storedGet(QLatin1String(UnknownName)), m_unknownPayload);
}

void RcloneDownloadTest::unknownSizeIsReadOnly()
{
    auto *job = KIO::storedPut(QByteArrayLiteral("replacement"), remoteUrl(QLatin1String(UnknownName)), -1, KIO::Overwrite | KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    QVERIFY(!job->exec());
    QCOMPARE(job->error(), int(KIO::ERR_WRITE_ACCESS_DENIED));
    delete job;

    QCOMPARE(storedGet(QLatin1String(UnknownName)), m_unknownPayload);
}

void RcloneDownloadTest::duplicateNamesCollapseAndDownloadSelectedId()
{
    KIO::UDSEntryList entries;
    auto *listJob = KIO::listDir(QUrl(QStringLiteral("rclone:/test")), KIO::HideProgressInfo);
    listJob->setUiDelegate(nullptr);
    listJob->setAutoDelete(false);
    connect(listJob, &KIO::ListJob::entries, this, [&entries](KIO::Job *, const KIO::UDSEntryList &batch) {
        entries.append(batch);
    });
    QVERIFY2(listJob->exec(), qPrintable(listJob->errorString()));
    delete listJob;

    KIO::UDSEntryList duplicates;
    std::copy_if(entries.cbegin(), entries.cend(), std::back_inserter(duplicates), [](const KIO::UDSEntry &entry) {
        return entry.stringValue(KIO::UDSEntry::UDS_NAME) == QLatin1String(DuplicateName);
    });
    QCOMPARE(duplicates.size(), 1);
    QCOMPARE(duplicates.constFirst().numberValue(KIO::UDSEntry::UDS_SIZE, -1), m_selectedPayload.size());
    QVERIFY(!(duplicates.constFirst().numberValue(KIO::UDSEntry::UDS_ACCESS) & S_IWUSR));

    const QByteArray downloaded = storedGet(QLatin1String(DuplicateName));
    QCOMPARE(downloaded, m_selectedPayload);
    QVERIFY(downloaded != m_olderPayload + m_selectedPayload);
}

void RcloneDownloadTest::duplicateNamesAreReadOnly()
{
    auto *job = KIO::storedPut(QByteArrayLiteral("replacement"), remoteUrl(QLatin1String(DuplicateName)), -1, KIO::Overwrite | KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    QVERIFY(!job->exec());
    QCOMPARE(job->error(), int(KIO::ERR_CANNOT_WRITE));
    delete job;

    QCOMPARE(storedGet(QLatin1String(DuplicateName)), m_selectedPayload);
}

QUrl RcloneDownloadTest::remoteUrl(QLatin1StringView name) const
{
    return QUrl(QStringLiteral("rclone:/test/") + name);
}

QByteArray RcloneDownloadTest::storedGet(QLatin1StringView name)
{
    auto *job = KIO::storedGet(remoteUrl(name), KIO::Reload, KIO::HideProgressInfo);
    job->setUiDelegate(nullptr);
    job->setAutoDelete(false);
    if (!job->exec()) {
        const QString error = job->errorString();
        delete job;
        QTest::qFail(qPrintable(error), __FILE__, __LINE__);
        return {};
    }

    const QByteArray data = job->data();
    delete job;
    return data;
}

QTEST_GUILESS_MAIN(RcloneDownloadTest)

#include "rclonedownloadtest.moc"
