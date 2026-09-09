#pragma once

#include "client.h"

#include <QByteArray>
#include <QIODevice>
#include <QTemporaryFile>

class RcloneRemoteFile
{
public:
    explicit RcloneRemoteFile(RcloneClient &client);

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

    void reset();

    RcloneClient &m_client;

    QString m_remoteSpec;

    QIODevice::OpenMode m_mode = QIODevice::NotOpen;

    qint64 m_position = 0;
    qint64 m_size = -1;

    bool m_dirty = false;
    bool m_hasLocalCopy = false;

    QTemporaryFile m_localFile;
};
