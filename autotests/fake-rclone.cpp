/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

namespace
{
constexpr qint64 PayloadSize = 16 * 1024 * 1024;
constexpr qsizetype ChunkSize = 16 * 1024;

bool writeAll(int descriptor, const char *data, qsizetype size)
{
    qsizetype offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(descriptor, data + offset, size_t(size - offset));
        if (written > 0) {
            offset += written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

void writeCounter(int descriptor, qint64 value)
{
    if (descriptor >= 0) {
        [[maybe_unused]] const ssize_t written = ::pwrite(descriptor, &value, sizeof(value), static_cast<off_t>(0));
    }
}

int openCounter()
{
    const QString path = qEnvironmentVariable("KIO_RCLONE_TEST_TRANSFER_COUNTER");
    if (path.isEmpty()) {
        return -1;
    }
    return ::open(QFile::encodeName(path).constData(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
}

int streamDownload()
{
    const int counter = openCounter();
    writeCounter(counter, 0);

    const QByteArray chunk(ChunkSize, 'R');
    qint64 transferred = 0;
    while (transferred < PayloadSize) {
        const qsizetype amount = qMin<qint64>(chunk.size(), PayloadSize - transferred);
        if (!writeAll(STDOUT_FILENO, chunk.constData(), amount)) {
            if (counter >= 0) {
                ::close(counter);
            }
            return 1;
        }
        transferred += amount;
        writeCounter(counter, transferred);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (counter >= 0) {
        ::close(counter);
    }
    return 0;
}

int consumeUpload()
{
    const int counter = openCounter();
    writeCounter(counter, 0);

    std::array<char, ChunkSize> chunk;
    qint64 transferred = 0;
    for (;;) {
        const ssize_t received = ::read(STDIN_FILENO, chunk.data(), chunk.size());
        if (received > 0) {
            transferred += received;
            writeCounter(counter, transferred);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (counter >= 0) {
            ::close(counter);
        }
        return received == 0 ? 0 : 1;
    }
}

int writeStat(const QString &remoteSpec)
{
    if (!remoteSpec.endsWith(QStringLiteral("payload.bin"))) {
        const QByteArray error("object not found\n");
        writeAll(STDERR_FILENO, error.constData(), error.size());
        return 1;
    }

    QJsonObject item{
        {QStringLiteral("Path"), QStringLiteral("payload.bin")},
        {QStringLiteral("Name"), QStringLiteral("payload.bin")},
        {QStringLiteral("Size"), PayloadSize},
        {QStringLiteral("MimeType"), QStringLiteral("application/octet-stream")},
        {QStringLiteral("ModTime"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("IsDir"), false},
    };
    const QByteArray json = QJsonDocument(item).toJson(QJsonDocument::Compact);
    return writeAll(STDOUT_FILENO, json.constData(), json.size()) ? 0 : 1;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2) {
        return 1;
    }

    const QString command = arguments.at(1);
    if (command == QLatin1String("lsjson")) {
        const QString remoteSpec = arguments.size() >= 3 ? arguments.at(2) : QString();
        if (arguments.contains(QStringLiteral("--stat"))) {
            return writeStat(remoteSpec);
        }
        const QByteArray emptyListing("[]");
        return writeAll(STDOUT_FILENO, emptyListing.constData(), emptyListing.size()) ? 0 : 1;
    }
    if (command == QLatin1String("cat")) {
        return streamDownload();
    }
    if (command == QLatin1String("rcat")) {
        return consumeUpload();
    }
    if (command == QLatin1String("listremotes")) {
        const QByteArray remotes(R"([{"name":"test","type":"drive","source":"file"}])");
        return writeAll(STDOUT_FILENO, remotes.constData(), remotes.size()) ? 0 : 1;
    }

    const QByteArray error = QByteArrayLiteral("unsupported fake command: ") + command.toUtf8() + '\n';
    writeAll(STDERR_FILENO, error.constData(), error.size());
    return 1;
}
