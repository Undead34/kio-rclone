/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KIO/FileCopyJob>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
constexpr auto CopytoBlockFile = ".kio-rclone-test-block-copyto";
constexpr auto CopytoFailFile = ".kio-rclone-test-fail-copyto";
constexpr auto CopytoStartedFile = ".kio-rclone-test-copyto-started";
constexpr auto DestinationName = "document.bin";
constexpr qsizetype UploadSize = 2 * 1024 * 1024 + 137;

QByteArray uploadPayload()
{
    QByteArray payload(UploadSize, Qt::Uninitialized);
    for (qsizetype index = 0; index < payload.size(); ++index) {
        payload[index] = char((index * 37 + index / 251) & 0xff);
    }
    return payload;
}

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
} // namespace

class RcloneUploadTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void originalStaysVisibleDuringBlockedUpload();
    void cancellationKeepsOriginal();
    void failureKeepsOriginal();
    void concurrentRemoteChangeIsReplacedAtomically();
    void successPublishesExactBytes();

private:
    [[nodiscard]] KIO::FileCopyJob *startUpload(const QByteArray &contents);
    [[nodiscard]] QString controlPath(QLatin1StringView name) const;
    [[nodiscard]] QString destinationPath() const;
    [[nodiscard]] QStringList remoteFiles() const;

    QTemporaryDir m_directory;
    QString m_remoteRoot;
    QString m_remoteDirectory;
    QString m_sourcePath;
};

void RcloneUploadTest::initTestCase()
{
    QVERIFY(m_directory.isValid());

    const QString pluginRoot = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(pluginRoot);
    qputenv("QT_PLUGIN_PATH", QFile::encodeName(pluginRoot));
    qputenv("KIO_RCLONE_EXECUTABLE", QByteArrayLiteral(FAKE_RCLONE_PATH));
    qunsetenv("KIO_RCLONE_TEST_TRANSFER_COUNTER");

    m_remoteRoot = m_directory.filePath(QStringLiteral("remote-root"));
    m_remoteDirectory = QDir(m_remoteRoot).filePath(QStringLiteral("test"));
    m_sourcePath = m_directory.filePath(QStringLiteral("source.bin"));
    qputenv("KIO_RCLONE_TEST_REMOTE_ROOT", QFile::encodeName(m_remoteRoot));
}

void RcloneUploadTest::init()
{
    QDir(m_remoteRoot).removeRecursively();
    QVERIFY(QDir().mkpath(m_remoteDirectory));
}

void RcloneUploadTest::cleanup()
{
    QFile::remove(controlPath(QLatin1String(CopytoBlockFile)));
    QFile::remove(controlPath(QLatin1String(CopytoFailFile)));
}

void RcloneUploadTest::originalStaysVisibleDuringBlockedUpload()
{
    const QByteArray original("the original document remains readable\n");
    const QByteArray replacement = uploadPayload();
    QVERIFY(writeFile(destinationPath(), original));
    QVERIFY(writeFile(controlPath(QLatin1String(CopytoBlockFile)), {}));

    KIO::FileCopyJob *job = startUpload(replacement);
    QVERIFY(job);
    QSignalSpy resultSpy(job, &KJob::result);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(controlPath(QLatin1String(CopytoStartedFile))) || !resultSpy.isEmpty(), 5000);
    QVERIFY2(resultSpy.isEmpty(), qPrintable(job->errorString()));

    QCOMPARE(readFile(destinationPath()), original);
    const QByteArray uploadSpec = readFile(controlPath(QLatin1String(CopytoStartedFile)));
    QCOMPARE(uploadSpec, QByteArrayLiteral("test:") + DestinationName);

    QVERIFY(QFile::remove(controlPath(QLatin1String(CopytoBlockFile))));
    if (resultSpy.isEmpty()) {
        QVERIFY(resultSpy.wait(10000));
    }
    QCOMPARE(job->error(), 0);
    QCOMPARE(readFile(destinationPath()), replacement);
    const QStringList expectedFiles{QLatin1String(DestinationName)};
    QCOMPARE(remoteFiles(), expectedFiles);
    delete job;
}

void RcloneUploadTest::cancellationKeepsOriginal()
{
    const QByteArray original("original bytes before cancellation\n");
    QVERIFY(writeFile(destinationPath(), original));
    QVERIFY(writeFile(controlPath(QLatin1String(CopytoBlockFile)), {}));

    KIO::FileCopyJob *job = startUpload(uploadPayload());
    QVERIFY(job);
    QSignalSpy resultSpy(job, &KJob::result);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(controlPath(QLatin1String(CopytoStartedFile))) || !resultSpy.isEmpty(), 5000);
    QVERIFY2(resultSpy.isEmpty(), qPrintable(job->errorString()));

    QVERIFY(job->kill(KJob::EmitResult));
    QFile::remove(controlPath(QLatin1String(CopytoBlockFile)));
    if (resultSpy.isEmpty()) {
        QVERIFY(resultSpy.wait(5000));
    }

    QCOMPARE(readFile(destinationPath()), original);
    const QStringList expectedFiles{QLatin1String(DestinationName)};
    QTRY_COMPARE_WITH_TIMEOUT(remoteFiles(), expectedFiles, 5000);
    delete job;
}

void RcloneUploadTest::failureKeepsOriginal()
{
    const QByteArray original("original bytes before failure\n");
    QVERIFY(writeFile(destinationPath(), original));
    QVERIFY(writeFile(controlPath(QLatin1String(CopytoFailFile)), {}));

    KIO::FileCopyJob *job = startUpload(uploadPayload());
    QVERIFY(job);
    QSignalSpy resultSpy(job, &KJob::result);
    if (resultSpy.isEmpty()) {
        QVERIFY(resultSpy.wait(10000));
    }

    QVERIFY(job->error() != 0);
    QCOMPARE(readFile(destinationPath()), original);
    const QStringList expectedFiles{QLatin1String(DestinationName)};
    QCOMPARE(remoteFiles(), expectedFiles);
    delete job;
}

void RcloneUploadTest::concurrentRemoteChangeIsReplacedAtomically()
{
    const QByteArray original("original before concurrent edit\n");
    const QByteArray concurrent("newer remote contents from another client\n");
    QVERIFY(writeFile(destinationPath(), original));
    const QByteArray replacement = uploadPayload();
    QVERIFY(writeFile(controlPath(QLatin1String(CopytoBlockFile)), {}));

    KIO::FileCopyJob *job = startUpload(replacement);
    QVERIFY(job);
    QSignalSpy resultSpy(job, &KJob::result);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(controlPath(QLatin1String(CopytoStartedFile))) || !resultSpy.isEmpty(), 5000);
    QVERIFY2(resultSpy.isEmpty(), qPrintable(job->errorString()));

    QTest::qWait(10);
    QVERIFY(writeFile(destinationPath(), concurrent));
    QCOMPARE(readFile(destinationPath()), concurrent);
    QVERIFY(QFile::remove(controlPath(QLatin1String(CopytoBlockFile))));
    if (resultSpy.isEmpty()) {
        QVERIFY(resultSpy.wait(10000));
    }

    QCOMPARE(job->error(), 0);
    QCOMPARE(readFile(destinationPath()), replacement);
    const QStringList expectedFiles{QLatin1String(DestinationName)};
    QCOMPARE(remoteFiles(), expectedFiles);
    delete job;
}

void RcloneUploadTest::successPublishesExactBytes()
{
    const QByteArray original("old contents\n");
    const QByteArray replacement = uploadPayload();
    QVERIFY(writeFile(destinationPath(), original));

    KIO::FileCopyJob *job = startUpload(replacement);
    QVERIFY(job);
    QSignalSpy resultSpy(job, &KJob::result);
    if (resultSpy.isEmpty()) {
        QVERIFY(resultSpy.wait(10000));
    }

    QCOMPARE(job->error(), 0);
    QCOMPARE(readFile(destinationPath()), replacement);
    const QStringList expectedFiles{QLatin1String(DestinationName)};
    QCOMPARE(remoteFiles(), expectedFiles);
    delete job;
}

KIO::FileCopyJob *RcloneUploadTest::startUpload(const QByteArray &contents)
{
    if (!writeFile(m_sourcePath, contents)) {
        return nullptr;
    }

    auto *job = KIO::file_copy(QUrl::fromLocalFile(m_sourcePath),
                               QUrl(QStringLiteral("rclone:/test/") + QLatin1String(DestinationName)),
                               -1,
                               KIO::HideProgressInfo | KIO::Overwrite);
    job->setAutoDelete(false);
    job->setUiDelegate(nullptr);
    return job;
}

QString RcloneUploadTest::controlPath(QLatin1StringView name) const
{
    return QDir(m_remoteRoot).filePath(name);
}

QString RcloneUploadTest::destinationPath() const
{
    return QDir(m_remoteDirectory).filePath(QLatin1String(DestinationName));
}

QStringList RcloneUploadTest::remoteFiles() const
{
    return QDir(m_remoteDirectory).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

QTEST_GUILESS_MAIN(RcloneUploadTest)

#include "rcloneuploadtest.moc"
