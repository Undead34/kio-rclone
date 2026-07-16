/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KIO/FileCopyJob>

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

namespace
{
constexpr qint64 PayloadSize = 16 * 1024 * 1024;
constexpr qint64 PauseThreshold = 256 * 1024;
constexpr qint64 MaximumPausedGrowth = 64 * 1024;
} // namespace

class RclonePauseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void downloadStopsAtTheProducer();
    void uploadStopsAtTheConsumer();

private:
    void verifyPause(KIO::FileCopyJob *job);
    [[nodiscard]] qint64 counterValue() const;

    QTemporaryDir m_directory;
    QString m_counterPath;
};

void RclonePauseTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    const QString pluginRoot = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(pluginRoot);
    qputenv("QT_PLUGIN_PATH", QFile::encodeName(pluginRoot));
    qputenv("KIO_RCLONE_EXECUTABLE", QByteArrayLiteral(FAKE_RCLONE_PATH));

    m_counterPath = m_directory.filePath(QStringLiteral("transfer-counter"));
    qputenv("KIO_RCLONE_TEST_TRANSFER_COUNTER", QFile::encodeName(m_counterPath));
}

void RclonePauseTest::downloadStopsAtTheProducer()
{
    const QString destination = m_directory.filePath(QStringLiteral("download.bin"));
    auto *job = KIO::file_copy(QUrl(QStringLiteral("rclone:/test/payload.bin")), QUrl::fromLocalFile(destination), -1, KIO::HideProgressInfo | KIO::Overwrite);

    verifyPause(job);
    QCOMPARE(QFileInfo(destination).size(), PayloadSize);
    delete job;
}

void RclonePauseTest::uploadStopsAtTheConsumer()
{
    const QString source = m_directory.filePath(QStringLiteral("upload.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray chunk(64 * 1024, 'U');
    for (qint64 written = 0; written < PayloadSize; written += chunk.size()) {
        QCOMPARE(file.write(chunk), chunk.size());
    }
    file.close();

    auto *job = KIO::file_copy(QUrl::fromLocalFile(source), QUrl(QStringLiteral("rclone:/test/uploaded.bin")), -1, KIO::HideProgressInfo | KIO::Overwrite);
    QSignalSpy infoSpy(job, &KJob::infoMessage);
    verifyPause(job);
    QCOMPARE(counterValue(), PayloadSize);
    QVERIFY(std::any_of(infoSpy.cbegin(), infoSpy.cend(), [](const QList<QVariant> &arguments) {
        return arguments.size() >= 2 && arguments.at(1).toString().contains(QStringLiteral("Finalizing upload"));
    }));
    delete job;
}

void RclonePauseTest::verifyPause(KIO::FileCopyJob *job)
{
    QVERIFY(job);
    job->setAutoDelete(false);
    job->setUiDelegate(nullptr);

    QSignalSpy resultSpy(job, &KJob::result);
    QTRY_VERIFY_WITH_TIMEOUT(counterValue() >= PauseThreshold || !resultSpy.isEmpty(), 5000);
    QVERIFY2(resultSpy.isEmpty(), qPrintable(job->errorString()));
    QVERIFY(job->suspend());

    QTest::qWait(300);
    const qint64 settledValue = counterValue();
    QTest::qWait(500);
    const qint64 pausedValue = counterValue();
    QVERIFY2(pausedValue - settledValue <= MaximumPausedGrowth,
             qPrintable(QStringLiteral("Transfer grew by %1 bytes while paused").arg(pausedValue - settledValue)));
    QVERIFY2(pausedValue < PayloadSize, "The producer consumed the complete transfer while KIO was paused");

    QVERIFY(job->resume());
    if (resultSpy.isEmpty()) {
        QVERIFY(resultSpy.wait(10000));
    }
    QCOMPARE(job->error(), 0);
}

qint64 RclonePauseTest::counterValue() const
{
    QFile counter(m_counterPath);
    if (!counter.open(QIODevice::ReadOnly)) {
        return 0;
    }

    qint64 value = 0;
    if (counter.read(reinterpret_cast<char *>(&value), sizeof(value)) != sizeof(value)) {
        return 0;
    }
    return value;
}

QTEST_GUILESS_MAIN(RclonePauseTest)

#include "rclonepausetest.moc"
