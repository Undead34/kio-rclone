#pragma once

#include "client.h"

#include <QByteArray>
#include <QDateTime>
#include <QIODevice>
#include <QSet>
#include <QTemporaryFile>

/**
 * @brief Policy for the local cache used by random-access FileJobs.
 *
 * Small files are materialized completely because one transfer is much
 * cheaper than starting rclone for every application read. Large files use a
 * sparse local file populated in blocks, so opening a video or disk image does
 * not require downloading it in full.
 */
struct RcloneFileCachePolicy {
    qint64 wholeFileLimit = 128LL * 1024 * 1024;
    qint64 readAheadBlockSize = 8LL * 1024 * 1024;
};

class RcloneRemoteFile
{
public:
    explicit RcloneRemoteFile(
        RcloneClient &client,
        RcloneFileCachePolicy cachePolicy = {});

    [[nodiscard]] RcloneStatus
    open(const QString &remoteSpec,
         QIODevice::OpenMode mode,
         const RcloneContext &ctx);

    [[nodiscard]] RcloneResponse<QByteArray>
    read(qint64 size,
         const RcloneContext &ctx);

    [[nodiscard]] RcloneStatus
    write(const QByteArray &data,
          const RcloneContext &ctx);

    [[nodiscard]] RcloneStatus
    seek(qint64 offset);

    [[nodiscard]] RcloneStatus
    truncate(qint64 size);

    [[nodiscard]] RcloneStatus
    flush(const RcloneContext &ctx);

    [[nodiscard]] RcloneStatus
    close(const RcloneContext &ctx);

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] qint64 position() const;
    [[nodiscard]] qint64 size() const;

private:
    [[nodiscard]] RcloneStatus prepareEmptyLocalCopy();

    [[nodiscard]] RcloneStatus
    ensureLocalCopy(const RcloneContext &ctx);

    [[nodiscard]] RcloneStatus prepareSparseLocalCache();

    [[nodiscard]] RcloneStatus
    ensureSparseRange(qint64 offset,
                      qint64 size,
                      const RcloneContext &ctx);

    [[nodiscard]] RcloneResponse<QByteArray>
    readLocal(qint64 size);

    /**
     * @brief Evita sobrescribir silenciosamente un archivo que cambió desde
     *        que se preparó su copia local de escritura.
     *
     * La comprobación se hace justo antes del único upload de close(). No es
     * una operación condicional atómica del proveedor, pero sí evita el caso
     * normal de editar una versión obsoleta durante una sesión FileJob.
     */
    [[nodiscard]] RcloneStatus
    verifyWriteTargetUnchanged(const RcloneContext &ctx) const;

    void reset();

    RcloneClient &m_client;
    RcloneFileCachePolicy m_cachePolicy;

    QString m_remoteSpec;

    QIODevice::OpenMode m_mode = QIODevice::NotOpen;

    qint64 m_position = 0;
    qint64 m_size = -1;

    bool m_dirty = false;
    bool m_hasLocalCopy = false;
    bool m_hasSparseCache = false;

    QSet<qint64> m_cachedBlocks;

    bool m_existed = false;
    QString m_originalId;
    QDateTime m_originalModificationTime;
    qint64 m_originalSize = -1;

    QTemporaryFile m_localFile;
};
