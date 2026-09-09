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

RcloneRemoteFile::RcloneRemoteFile(RcloneClient &client)
    : m_client(client)
{
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
        } else if (writable) {
            const RcloneStatus status = ensureLocalCopy(ctx);

            if (!status.success()) {
                reset();
                return status;
            }

            if (mode.testFlag(QIODevice::Append)) {
                m_position = m_size;

                if (!m_localFile.seek(m_position)) {
                    const QString error = m_localFile.errorString();
                    reset();
                    return failure(RcloneErrorCode::ProcessFailure, error);
                }
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

    /*
     * A writable FileJob operates on the local staging copy. A read-only
     * FileJob stays remote and asks rclone only for the range KIO requested.
     */
    if (canWrite(m_mode)) {
        const RcloneStatus status = ensureLocalCopy(ctx);

        if (!status.success()) {
            return {std::nullopt, status.error};
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

    if (m_size >= 0 && m_position >= m_size) {
        return readSuccess({});
    }

    qint64 requested = size;

    if (m_size >= 0) {
        requested = qMin(requested, m_size - m_position);
    }

    QByteArray data;
    const RcloneStatus status = m_client.downloadRange(
        m_remoteSpec,
        m_position,
        requested,
        [&data](const QByteArray &chunk) {
            data += chunk;
            return true;
        },
        ctx);

    if (!status.success()) {
        return {std::nullopt, status.error};
    }

    m_position += data.size();
    return readSuccess(data);
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

    if (m_hasLocalCopy) {
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
    if (!isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
    }

    if (!m_dirty) {
        return success();
    }

    const RcloneStatus localStatus = ensureLocalCopy(ctx);

    if (!localStatus.success()) {
        return localStatus;
    }

    if (!m_localFile.flush()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    const QString localPath = m_localFile.fileName();

    RcloneWriteOptions options;
    options.replaceExisting = true;

    const RcloneStatus status = m_client.upload(
        localPath,
        m_remoteSpec,
        {},
        options,
        ctx);

    if (!status.success()) {
        return status;
    }

    m_dirty = false;
    return success();
}

RcloneStatus
RcloneRemoteFile::close(const RcloneContext &ctx)
{
    if (!isOpen()) {
        return success();
    }

    const RcloneStatus status = flush(ctx);

    if (!status.success()) {
        return status;
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

    m_size = m_localFile.size();
    m_hasLocalCopy = true;

    if (!m_localFile.seek(m_position)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    return success();
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
}
