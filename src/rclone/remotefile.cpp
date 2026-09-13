/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "remotefile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileDevice>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace {

RcloneStatus success()
{
    return {
        true,
        {RcloneErrorCode::None, {}, 0},
    };
}

RcloneStatus failure(RcloneErrorCode code,
                     const QString &message)
{
    return {
        false,
        {code, message, -1},
    };
}

RcloneResponse<QByteArray> readFailure(RcloneErrorCode code,
                                       const QString &message)
{
    return {
        std::nullopt,
        {code, message, -1},
    };
}

RcloneResponse<QByteArray> readSuccess(QByteArray data)
{
    return {
        std::move(data),
        {RcloneErrorCode::None, {}, 0},
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

bool snapshotsMatch(const RcloneTargetSnapshot &cached,
                    const RcloneTargetSnapshot &current)
{
    if (cached.exists != current.exists
        || cached.isDirectory != current.isDirectory) {
        return false;
    }

    if (!cached.exists) {
        return true;
    }

    bool comparedVersion = false;

    if (!cached.id.isEmpty() && !current.id.isEmpty()) {
        comparedVersion = true;

        if (cached.id != current.id) {
            return false;
        }
    }

    if (cached.modificationTime.isValid()
        && current.modificationTime.isValid()) {
        comparedVersion = true;

        if (cached.modificationTime != current.modificationTime) {
            return false;
        }
    }

    if (cached.size >= 0 && current.size >= 0) {
        comparedVersion = true;

        if (cached.size != current.size) {
            return false;
        }
    }

    // Without a comparable version signal, use the remote as the source of
    // truth and download it again rather than risk returning stale bytes.
    return comparedVersion;
}

bool ensurePrivateDirectory(const QString &path,
                           QString *error)
{
    if (!QDir().mkpath(path)) {
        *error = QStringLiteral("Could not create the local cache directory");
        return false;
    }

    const QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner
        | QFileDevice::WriteOwner
        | QFileDevice::ExeOwner;

    if (!QFile::setPermissions(path, permissions)) {
        *error = QStringLiteral("Could not secure the local cache directory");
        return false;
    }

    return true;
}

RcloneStatus createEmptyLocalFile(const QString &path)
{
    QString error;

    if (!ensurePrivateDirectory(QFileInfo(path).absolutePath(), &error)) {
        return failure(RcloneErrorCode::ProcessFailure, error);
    }

    QSaveFile output(path);

    if (!output.open(QIODevice::WriteOnly)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            output.errorString());
    }

    if (!output.commit()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            output.errorString());
    }

    const QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;

    if (!QFile::setPermissions(path, permissions)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("Could not secure the local cache file"));
    }

    return success();
}

} // namespace

RcloneRemoteFile::RcloneRemoteFile(
    RcloneClient &client,
    RcloneSyncRepository &repository,
    RcloneCacheConfig config)
    : m_client(client)
    , m_repository(repository)
    , m_config(std::move(config))
{
    if (m_config.baseCacheDirectory.isEmpty()) {
        m_config.baseCacheDirectory = QDir(
            QStandardPaths::writableLocation(
                QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("kio-rclone/files"));
    }

    m_config.baseCacheDirectory =
        QDir(m_config.baseCacheDirectory).absolutePath();
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

    if ((!readable && !writable) || remoteSpec.isEmpty()) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            QStringLiteral("The remote file path or open mode is invalid"));
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
            QStringLiteral("NewOnly and ExistingOnly cannot be combined"));
    }

    m_remoteSpec = remoteSpec;
    m_localPath = resolveLocalPath(remoteSpec);
    m_originalTarget = {};
    m_state = RcloneFileSyncState::CloudOnly;

    const auto cachedEntry = m_repository.find(remoteSpec);

    if (cachedEntry && !cachedEntry->localPath.isEmpty()) {
        m_localPath = cachedEntry->localPath;
    }

    const auto item = m_client.stat(remoteSpec, ctx);
    bool localFileReady = false;

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

        const RcloneTargetSnapshot currentTarget =
            RcloneTargetSnapshot::fromItem(*item.data);

        if (cachedEntry && cachedEntry->isDirty()) {
            // Keep the original base version so a later upload cannot silently
            // replace remote changes made since this local copy was edited.
            m_originalTarget = cachedEntry->remoteSnapshot;

            if (cachedEntry->state == RcloneSyncState::Conflict
                || !snapshotsMatch(cachedEntry->remoteSnapshot,
                                   currentTarget)
                || !QFileInfo(m_localPath).isFile()) {
                updateState(RcloneFileSyncState::Conflict);
                reset();
                return failure(
                    RcloneErrorCode::AlreadyExists,
                    QStringLiteral(
                        "The local file conflicts with a changed remote file"));
            }

            m_state = RcloneFileSyncState::Modified;
            localFileReady = true;
        } else {
            m_originalTarget = currentTarget;

            if (cachedEntry
                && cachedEntry->state == RcloneSyncState::Synced
                && snapshotsMatch(cachedEntry->remoteSnapshot,
                                  currentTarget)
                && QFileInfo(m_localPath).isFile()) {
                m_state = RcloneFileSyncState::Synced;
                localFileReady = true;
            } else {
                const RcloneStatus downloadStatus =
                    ensureFullyDownloaded(ctx);

                if (!downloadStatus.success()) {
                    reset();
                    return downloadStatus;
                }

                updateState(RcloneFileSyncState::Synced);
                localFileReady = true;
            }
        }
    } else if (item.error.code == RcloneErrorCode::NotFound) {
        if (cachedEntry && cachedEntry->isDirty()) {
            m_originalTarget = cachedEntry->remoteSnapshot;

            if (cachedEntry->remoteSnapshot.exists
                || cachedEntry->state == RcloneSyncState::Conflict
                || !QFileInfo(m_localPath).isFile()) {
                updateState(RcloneFileSyncState::Conflict);
                reset();
                return failure(
                    RcloneErrorCode::NotFound,
                    QStringLiteral(
                        "The remote file was removed while local changes were pending"));
            }

            m_state = RcloneFileSyncState::Modified;
            localFileReady = true;
        } else {
            if (!writable || mode.testFlag(QIODevice::ExistingOnly)) {
                const RcloneError error = item.error;
                reset();
                return {false, error};
            }

            m_originalTarget = {};
            const RcloneStatus createStatus =
                createEmptyLocalFile(m_localPath);

            if (!createStatus.success()) {
                reset();
                return createStatus;
            }

            m_state = RcloneFileSyncState::Modified;
            localFileReady = true;
        }
    } else {
        const RcloneError error = item.error;
        reset();
        return {false, error};
    }

    if (!localFileReady) {
        reset();
        return failure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("The local cache file is unavailable"));
    }

    QString directoryError;

    if (!ensurePrivateDirectory(
            QFileInfo(m_localPath).absolutePath(),
            &directoryError)) {
        reset();
        return failure(RcloneErrorCode::ProcessFailure, directoryError);
    }

    m_localFile.setFileName(m_localPath);

    if (!m_localFile.open(QIODevice::ReadWrite)) {
        const QString error = m_localFile.errorString();
        reset();
        return failure(RcloneErrorCode::ProcessFailure, error);
    }

    const QFileDevice::Permissions filePermissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;

    if (!QFile::setPermissions(m_localPath, filePermissions)) {
        const QString error =
            QStringLiteral("Could not secure the local cache file");
        reset();
        return failure(RcloneErrorCode::ProcessFailure, error);
    }

    m_mode = mode;

    if (mode.testFlag(QIODevice::Truncate)
        && !m_localFile.resize(0)) {
        const QString error = m_localFile.errorString();
        reset();
        return failure(RcloneErrorCode::ProcessFailure, error);
    }

    if (mode.testFlag(QIODevice::Append)
        && !m_localFile.seek(m_localFile.size())) {
        const QString error = m_localFile.errorString();
        reset();
        return failure(RcloneErrorCode::ProcessFailure, error);
    }

    if (mode.testFlag(QIODevice::Truncate)
        || (m_state == RcloneFileSyncState::Modified
            && (!cachedEntry || !cachedEntry->isDirty()))) {
        updateState(RcloneFileSyncState::Modified);
    }

    return success();
}

RcloneResponse<QByteArray>
RcloneRemoteFile::read(qint64 size)
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

    const QByteArray data = m_localFile.read(size);

    if (data.isEmpty()
        && m_localFile.error() != QFileDevice::NoError) {
        return readFailure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    return readSuccess(data);
}

RcloneStatus
RcloneRemoteFile::write(const QByteArray &data)
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

    if (m_mode.testFlag(QIODevice::Append)
        && !m_localFile.seek(m_localFile.size())) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    const qint64 written = m_localFile.write(data);

    if (written > 0) {
        updateState(RcloneFileSyncState::Modified);
    }

    if (written != data.size()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    if (data.isEmpty()) {
        updateState(RcloneFileSyncState::Modified);
    }

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

    if (!m_localFile.seek(offset)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

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

    if (!m_localFile.resize(size)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            m_localFile.errorString());
    }

    updateState(RcloneFileSyncState::Modified);
    return success();
}

RcloneStatus
RcloneRemoteFile::flush()
{
    if (!isOpen()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("No remote file is open"));
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

    if (!canWrite(m_mode)
        || m_state != RcloneFileSyncState::Modified) {
        reset();
        return success();
    }

    const RcloneStatus flushStatus = flush();

    if (!flushStatus.success()) {
        m_localFile.close();
        m_mode = QIODevice::NotOpen;
        return flushStatus;
    }

    updateState(RcloneFileSyncState::SyncingUp);
    const RcloneStatus uploadStatus = performUpload(ctx);

    if (!uploadStatus.success()) {
        updateState(RcloneFileSyncState::Modified);
        m_localFile.close();
        m_mode = QIODevice::NotOpen;
        return uploadStatus;
    }

    updateState(RcloneFileSyncState::Synced);
    reset();
    return success();
}

void RcloneRemoteFile::evictLocalCache()
{
    if (m_remoteSpec.isEmpty()) {
        return;
    }

    const auto entry = m_repository.find(m_remoteSpec);

    if (entry && entry->isDirty()) {
        return;
    }

    m_localFile.close();
    m_mode = QIODevice::NotOpen;
    QFile::remove(m_localPath);
    m_repository.remove(m_remoteSpec);
    m_state = RcloneFileSyncState::CloudOnly;
}

bool RcloneRemoteFile::isOpen() const
{
    return m_mode != QIODevice::NotOpen && m_localFile.isOpen();
}

qint64 RcloneRemoteFile::position() const
{
    return isOpen() ? m_localFile.pos() : 0;
}

qint64 RcloneRemoteFile::size() const
{
    return isOpen() ? m_localFile.size() : -1;
}

RcloneFileSyncState RcloneRemoteFile::state() const
{
    return m_state;
}

QString RcloneRemoteFile::localCachePath() const
{
    return m_localPath;
}

RcloneStatus
RcloneRemoteFile::ensureFullyDownloaded(const RcloneContext &ctx)
{
    updateState(RcloneFileSyncState::SyncingDown);

    QString directoryError;

    if (!ensurePrivateDirectory(
            QFileInfo(m_localPath).absolutePath(),
            &directoryError)) {
        return failure(RcloneErrorCode::ProcessFailure, directoryError);
    }

    QSaveFile output(m_localPath);
    output.setDirectWriteFallback(false);

    if (!output.open(QIODevice::WriteOnly)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            output.errorString());
    }

    bool localWriteFailed = false;
    QString localError;
    qint64 received = 0;

    const RcloneStatus downloadStatus = m_client.download(
        m_remoteSpec,
        [&output, &localWriteFailed, &localError, &received](
            const QByteArray &chunk) {
            if (output.write(chunk) != chunk.size()) {
                localWriteFailed = true;
                localError = output.errorString();
                return false;
            }

            received += chunk.size();
            return true;
        },
        ctx);

    if (localWriteFailed) {
        return failure(RcloneErrorCode::ProcessFailure, localError);
    }

    if (!downloadStatus.success()) {
        return downloadStatus;
    }

    if (m_originalTarget.size >= 0
        && received != m_originalTarget.size) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("The remote file changed while it was being cached"));
    }

    if (!output.commit()) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            output.errorString());
    }

    const QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;

    if (!QFile::setPermissions(m_localPath, permissions)) {
        return failure(
            RcloneErrorCode::ProcessFailure,
            QStringLiteral("Could not secure the local cache file"));
    }

    return success();
}

RcloneStatus
RcloneRemoteFile::performUpload(const RcloneContext &ctx)
{
    RcloneWriteOptions options;
    options.replaceExisting = m_originalTarget.exists;
    options.expectedTarget = m_originalTarget;

    const RcloneStatus status = m_client.upload(
        m_localPath,
        m_remoteSpec,
        {},
        options,
        ctx);

    if (!status.success()) {
        return status;
    }

    const auto uploadedItem = m_client.stat(m_remoteSpec, ctx);

    if (uploadedItem.success() && !uploadedItem.data->isDirectory) {
        m_originalTarget =
            RcloneTargetSnapshot::fromItem(*uploadedItem.data);
    }

    return success();
}

void RcloneRemoteFile::reset()
{
    m_localFile.close();
    m_localFile.setFileName({});
    m_mode = QIODevice::NotOpen;
    m_remoteSpec.clear();
    m_localPath.clear();
    m_state = RcloneFileSyncState::CloudOnly;
    m_originalTarget = {};
}

void RcloneRemoteFile::updateState(RcloneFileSyncState newState)
{
    m_state = newState;

    // Transitional states are process-local. Keep the last recoverable
    // repository state until a download/upload finishes.
    if (m_remoteSpec.isEmpty()
        || newState == RcloneFileSyncState::SyncingDown
        || newState == RcloneFileSyncState::SyncingUp) {
        return;
    }

    RcloneSyncEntry entry;
    entry.remoteSpec = m_remoteSpec;
    entry.localPath = m_localPath;
    entry.remoteSnapshot = m_originalTarget;
    entry.lastSyncTime = QDateTime::currentDateTimeUtc();

    switch (newState) {
    case RcloneFileSyncState::CloudOnly:
        entry.state = RcloneSyncState::CloudOnly;
        break;

    case RcloneFileSyncState::Synced:
        entry.state = RcloneSyncState::Synced;
        break;

    case RcloneFileSyncState::Modified:
        entry.state = RcloneSyncState::Modified;
        break;

    case RcloneFileSyncState::Conflict:
        entry.state = RcloneSyncState::Conflict;
        break;

    case RcloneFileSyncState::SyncingDown:
    case RcloneFileSyncState::SyncingUp:
        return;
    }

    m_repository.save(entry);
}

QString RcloneRemoteFile::resolveLocalPath(const QString &remoteSpec) const
{
    const QByteArray digest = QCryptographicHash::hash(
        remoteSpec.toUtf8(),
        QCryptographicHash::Sha256);

    return QDir(m_config.baseCacheDirectory).filePath(
        QString::fromLatin1(digest.toHex()));
}
