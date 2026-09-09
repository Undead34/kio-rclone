/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "remotefile.h"

#include <QFileDevice>

#include <utility>

namespace {

RcloneStatus success()
{
    return {
        true,
        {
            RcloneErrorCode::None,
            {},
            0,
        },
    };
}

RcloneStatus failure(RcloneErrorCode code,
                     const QString &message)
{
    return {
        false,
        {
            code,
            message,
            -1,
        },
    };
}

RcloneResponse<QByteArray> readFailure(RcloneErrorCode code,
                                       const QString &message)
{
    return {
        std::nullopt,
        {
            code,
            message,
            -1,
        },
    };
}

RcloneResponse<QByteArray> readSuccess(QByteArray data)
{
    return {
        std::move(data),
        {
            RcloneErrorCode::None,
            {},
            0,
        },
    };
}

bool canRead(QIODevice::OpenMode mode)
{
    return mode.testFlag(QIODevice::ReadOnly);
}

bool canWrite(QIODevice::OpenMode mode)
{
    return mode.testFlag(QIODevice::WriteOnly);
}

} // namespace

RcloneRemoteFile::RcloneRemoteFile(
    RcloneClient &client,
    RcloneFileCachePolicy cachePolicy)
    : m_client(client)
    , m_cachePolicy(cachePolicy)
{
    m_cachePolicy.wholeFileLimit =
        qMax<qint64>(0, m_cachePolicy.wholeFileLimit);
    m_cachePolicy.readAheadBlockSize =
        qMax<qint64>(64 * 1024, m_cachePolicy.readAheadBlockSize);
    m_localFile.setAutoRemove(true);
}

RcloneStatus
RcloneRemoteFile::open(const QString &remoteSpec,
                       QIODevice::OpenMode mode,
                       const RcloneContext &ctx)
{
    if (isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("A remote file is already open"));
    }

    const bool readable = canRead(mode);
    const bool writable = canWrite(mode);

    if (!readable && !writable) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("The open mode does not allow reading or writing"));
    }

    if (remoteSpec.isEmpty()) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("The remote file path is empty"));
    }

    if ((mode.testFlag(QIODevice::Append)
         || mode.testFlag(QIODevice::Truncate)
         || mode.testFlag(QIODevice::NewOnly))
        && !writable) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("The requested open mode requires write access"));
    }

    if (mode.testFlag(QIODevice::NewOnly)
        && mode.testFlag(QIODevice::ExistingOnly)) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("NewOnly and ExistingOnly cannot be used together"));
    }

    m_remoteSpec = remoteSpec;
    m_mode = mode;
    m_position = 0;
    m_size = -1;
    m_dirty = false;
    m_hasLocalCopy = false;
    m_hasSparseCache = false;
    m_cachedBlocks.clear();
    m_originalTarget = {};

    const auto item = m_client.stat(remoteSpec, ctx);

    if (item.success()) {
        if (item.data->isDirectory) {
            reset();
            return failure(
                RcloneErrorCode::Unsupported,
                QStringLiteral("The remote path is a directory"));
        }

        if (mode.testFlag(QIODevice::NewOnly)) {
            reset();
            return failure(
                RcloneErrorCode::AlreadyExists,
                QStringLiteral("The remote file already exists"));
        }

        if (writable && item.data->readOnly) {
            reset();
            return failure(
                RcloneErrorCode::PermissionDenied,
                QStringLiteral("The remote file is read-only"));
        }

        m_originalTarget =
            RcloneTargetSnapshot::fromItem(*item.data);
        m_size = item.data->size;

        if (mode.testFlag(QIODevice::Truncate)) {
            const RcloneStatus status = prepareEmptyLocalCopy();

            if (!status.success()) {
                reset();
                return status;
            }

            // Opening with Truncate must replace the remote file even when no
            // subsequent write() arrives before close().
            m_dirty = true;
        } else if (writable
                   || m_size < 0
                   || m_size <= m_cachePolicy.wholeFileLimit) {
            const RcloneStatus status = ensureLocalCopy(ctx);

            if (!status.success()) {
                reset();
                return status;
            }

            if (writable && mode.testFlag(QIODevice::Append)) {
                m_position = m_size;

                if (!m_localFile.seek(m_position)) {
                    const QString error = m_localFile.errorString();
                    reset();
                    return failure(RcloneErrorCode::ProcessFailure, error);
                }
            }
        } else {
            const RcloneStatus status = prepareSparseLocalCache();

            if (!status.success()) {
                reset();
                return status;
            }
        }

        return success();
    }

    if (item.error.code != RcloneErrorCode::NotFound) {
        const RcloneError error = item.error;
        reset();
        return {false, error};
    }

    if (!writable || mode.testFlag(QIODevice::ExistingOnly)) {
        const RcloneError error = item.error;
        reset();
        return {false, error};
    }

    const RcloneStatus status = prepareEmptyLocalCopy();

    if (!status.success()) {
        reset();
        return status;
    }

    // QIODevice opens a missing path for writing as an empty file. Delay the
    // actual remote creation until close(), but do not lose that intent.
    m_dirty = true;
    return success();
}

RcloneResponse<QByteArray>
RcloneRemoteFile::read(qint64 size,
                       const RcloneContext &ctx)
{
    if (!isOpen()) {
        return readFailure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
    }

    if (!canRead(m_mode)) {
        return readFailure(
            RcloneErrorCode::PermissionDenied,
            QStringLiteral("The remote file was not opened for reading"));
    }

    if (size < 0) {
        return readFailure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("The requested read size is invalid"));
    }

    if (size == 0) {
        return readSuccess({});
    }

    if (m_size >= 0 && m_position >= m_size) {
        return readSuccess({});
    }

    qint64 requested = size;

    if (m_size >= 0) {
        requested = qMin(requested, m_size - m_position);
    }

    if (m_hasSparseCache) {
        const RcloneStatus status = ensureSparseRange(
            m_position,
            requested,
            ctx);

        if (!status.success()) {
            return {std::nullopt, status.error};
        }
    } else if (!m_hasLocalCopy) {
        const RcloneStatus status = ensureLocalCopy(ctx);

        if (!status.success()) {
            return {std::nullopt, status.error};
        }
    }

    return readLocal(requested);
}

RcloneStatus
RcloneRemoteFile::write(const QByteArray &data,
                        const RcloneContext &ctx)
{
    if (!isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
    }

    if (!canWrite(m_mode)) {
        return failure(
            RcloneErrorCode::PermissionDenied,
            QStringLiteral("The remote file was not opened for writing"));
    }

    const RcloneStatus status = ensureLocalCopy(ctx);

    if (!status.success()) {
        return status;
    }

    if (m_mode.testFlag(QIODevice::Append)) {
        m_position = m_localFile.size();
    }

    if (!m_localFile.seek(m_position)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    const qint64 written = m_localFile.write(data);

    if (written != data.size()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    m_position = m_localFile.pos();
    m_size = m_localFile.size();
    m_dirty = true;
    return success();
}

RcloneStatus
RcloneRemoteFile::seek(qint64 offset)
{
    if (!isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
    }

    if (offset < 0) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("The requested offset is invalid"));
    }

    if (m_hasLocalCopy || m_hasSparseCache) {
        if (!m_localFile.isOpen()
            && !m_localFile.open()) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                m_localFile.errorString());
        }

        if (!m_localFile.seek(offset)) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                m_localFile.errorString());
        }
    }

    m_position = offset;
    return success();
}

RcloneStatus
RcloneRemoteFile::truncate(qint64 size)
{
    if (!isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
    }

    if (!canWrite(m_mode)) {
        return failure(
            RcloneErrorCode::PermissionDenied,
            QStringLiteral("The remote file was not opened for writing"));
    }

    if (size < 0) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("The requested size is invalid"));
    }

    // open() prepares the local copy for every writable mode, so truncate()
    // deliberately needs no context and never starts a second remote process.
    if (!m_hasLocalCopy) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("The writable staging file is unavailable"));
    }

    if (!m_localFile.isOpen()
        && !m_localFile.open()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (!m_localFile.resize(size)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    m_size = size;
    m_dirty = true;
    return success();
}

RcloneStatus
RcloneRemoteFile::flush(const RcloneContext &ctx)
{
    Q_UNUSED(ctx)

    if (!isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
    }

    // FileJob writes are staged locally. A flush must never turn into a
    // network upload: applications such as LibreOffice may issue many flushes
    // while a document is still being edited.
    if (!m_hasLocalCopy) {
        return success();
    }

    if (!m_localFile.isOpen()
        && !m_localFile.open()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (!m_localFile.flush()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    return success();
}

RcloneStatus
RcloneRemoteFile::close(const RcloneContext &ctx)
{
    if (!isOpen()) {
        return success();
    }

    if (!m_dirty) {
        reset();
        return success();
    }

    const RcloneStatus localStatus = flush(ctx);

    if (!localStatus.success()) {
        return localStatus;
    }

    RcloneWriteOptions options;
    options.replaceExisting = m_originalTarget.exists;
    options.expectedTarget = m_originalTarget;

    const RcloneStatus uploadStatus = m_client.upload(
        m_localFile.fileName(),
        m_remoteSpec,
        {},
        options,
        ctx);

    if (!uploadStatus.success()) {
        return uploadStatus;
    }

    reset();
    return success();
}

bool RcloneRemoteFile::isOpen() const
{
    return m_mode != QIODevice::NotOpen;
}

qint64 RcloneRemoteFile::position() const
{
    return m_position;
}

qint64 RcloneRemoteFile::size() const
{
    return m_size;
}

RcloneStatus
RcloneRemoteFile::prepareEmptyLocalCopy()
{
    if (!m_localFile.isOpen()
        && !m_localFile.open()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (!m_localFile.resize(0)
        || !m_localFile.seek(0)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    m_position = 0;
    m_size = 0;
    m_hasLocalCopy = true;
    m_hasSparseCache = false;
    m_cachedBlocks.clear();
    return success();
}

RcloneStatus
RcloneRemoteFile::ensureLocalCopy(const RcloneContext &ctx)
{
    if (m_hasLocalCopy) {
        if (!m_localFile.isOpen()
            && !m_localFile.open()) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                m_localFile.errorString());
        }

        if (!m_localFile.seek(m_position)) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                m_localFile.errorString());
        }

        return success();
    }

    if (!m_localFile.isOpen()
        && !m_localFile.open()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (!m_localFile.resize(0)
        || !m_localFile.seek(0)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (m_size != 0) {
        bool localWriteFailed = false;
        QString localError;

        const RcloneStatus status = m_client.download(
            m_remoteSpec,
            [this, &localWriteFailed, &localError](const QByteArray &chunk) {
                if (m_localFile.write(chunk) != chunk.size()) {
                    localWriteFailed = true;
                    localError = m_localFile.errorString();
                    return false;
                }

                return true;
            },
            ctx);

        if (localWriteFailed) {
            return failure(RcloneErrorCode::ProcessFailure, localError);
        }

        if (!status.success()) {
            return status;
        }
    }

    const qint64 downloadedSize = m_localFile.size();

    if (m_size >= 0 && downloadedSize != m_size) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("The remote file changed while it was being cached"));
    }

    m_size = downloadedSize;
    m_hasLocalCopy = true;
    m_hasSparseCache = false;
    m_cachedBlocks.clear();

    if (!m_localFile.seek(m_position)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    return success();
}

RcloneStatus
RcloneRemoteFile::prepareSparseLocalCache()
{
    if (m_size < 0) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("A sparse cache requires a known file size"));
    }

    if (!m_localFile.isOpen()
        && !m_localFile.open()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    // QFile::resize() uses a sparse extension on the local filesystems where
    // it is supported; no remote bytes are fetched here.
    if (!m_localFile.resize(m_size)
        || !m_localFile.seek(m_position)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    m_hasLocalCopy = false;
    m_hasSparseCache = true;
    m_cachedBlocks.clear();
    return success();
}

RcloneStatus
RcloneRemoteFile::ensureSparseRange(
    qint64 offset,
    qint64 size,
    const RcloneContext &ctx)
{
    if (!m_hasSparseCache || offset < 0 || size <= 0
        || m_size < 0 || offset > m_size - size) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("The requested cache range is invalid"));
    }

    const qint64 blockSize = m_cachePolicy.readAheadBlockSize;
    const qint64 firstBlock = offset / blockSize;
    const qint64 lastBlock = (offset + size - 1) / blockSize;
    const qint64 finalFileBlock = m_size > 0
        ? (m_size - 1) / blockSize
        : 0;

    qint64 block = firstBlock;

    while (block <= lastBlock) {
        if (m_cachedBlocks.contains(block)) {
            ++block;
            continue;
        }

        const qint64 runFirstBlock = block;

        while (block < lastBlock
               && !m_cachedBlocks.contains(block + 1)) {
            ++block;
        }

        const qint64 runLastBlock = block;
        const qint64 fetchOffset = runFirstBlock * blockSize;
        const qint64 fetchEnd = runLastBlock >= finalFileBlock
            ? m_size
            : (runLastBlock + 1) * blockSize;
        const qint64 fetchSize = fetchEnd - fetchOffset;

        if (!m_localFile.seek(fetchOffset)) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                m_localFile.errorString());
        }

        qint64 received = 0;
        QString localError;

        const RcloneStatus status = m_client.downloadRange(
            m_remoteSpec,
            fetchOffset,
            fetchSize,
            [this, fetchSize, &received, &localError](
                const QByteArray &chunk) {
                if (chunk.size() > fetchSize - received
                    || m_localFile.write(chunk) != chunk.size()) {
                    localError = chunk.size() > fetchSize - received
                        ? QStringLiteral("Rclone returned more data than requested")
                        : m_localFile.errorString();
                    return false;
                }

                received += chunk.size();
                return true;
            },
            ctx);

        if (!localError.isEmpty()) {
            return failure(RcloneErrorCode::ProcessFailure, localError);
        }

        if (!status.success()) {
            return status;
        }

        if (received != fetchSize) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                QStringLiteral("The remote file changed while it was being cached"));
        }

        if (!m_localFile.flush()) {
            return failure(
                RcloneErrorCode::ProcessFailure,
                m_localFile.errorString());
        }

        for (qint64 cached = runFirstBlock;
             cached <= runLastBlock;
             ++cached) {
            m_cachedBlocks.insert(cached);
        }

        ++block;
    }

    return success();
}

RcloneResponse<QByteArray>
RcloneRemoteFile::readLocal(qint64 size)
{
    if (!m_localFile.isOpen()
        && !m_localFile.open()) {
        return readFailure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (!m_localFile.seek(m_position)) {
        return readFailure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    const QByteArray data = m_localFile.read(size);

    if (data.isEmpty()
        && m_localFile.error() != QFileDevice::NoError) {
        return readFailure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    m_position = m_localFile.pos();
    return readSuccess(data);
}

void RcloneRemoteFile::reset()
{
    m_localFile.close();
    m_localFile.remove();

    m_remoteSpec.clear();
    m_mode = QIODevice::NotOpen;
    m_position = 0;
    m_size = -1;
    m_dirty = false;
    m_hasLocalCopy = false;
    m_hasSparseCache = false;
    m_cachedBlocks.clear();
    m_originalTarget = {};
}
