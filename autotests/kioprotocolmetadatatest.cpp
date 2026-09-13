/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QJsonObject>
#include <QPluginLoader>
#include <QTest>

namespace {

QJsonObject rcloneProtocolMetadata()
{
    QPluginLoader loader(QString::fromUtf8(KIO_RCLONE_WORKER_PLUGIN));
    const QJsonObject pluginMetadata = loader.metaData();
    return pluginMetadata.value(QStringLiteral("MetaData"))
        .toObject()
        .value(QStringLiteral("KDE-KIO-Protocols"))
        .toObject()
        .value(QStringLiteral("rclone"))
        .toObject();
}

} // namespace

class KioProtocolMetadataTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void doesNotAdvertisePerOperationFileJobs()
    {
        const QJsonObject protocol = rcloneProtocolMetadata();

        QVERIFY2(!protocol.isEmpty(),
                 "The worker plugin does not contain rclone protocol metadata");
        QCOMPARE(protocol.value(QStringLiteral("Class")).toString(),
                 QStringLiteral(":local"));
        QVERIFY2(protocol.contains(QStringLiteral("opening")),
                 "The opening capability must be explicit");
        QVERIFY2(protocol.contains(QStringLiteral("truncating")),
                 "The truncating capability must be explicit");
        QVERIFY2(protocol.value(QStringLiteral("opening")).isBool(),
                 "The opening capability must be a JSON boolean");
        QVERIFY2(protocol.value(QStringLiteral("truncating")).isBool(),
                 "The truncating capability must be a JSON boolean");
        QVERIFY(!protocol.value(QStringLiteral("opening")).toBool());
        QVERIFY(!protocol.value(QStringLiteral("truncating")).toBool());
    }
};

QTEST_MAIN(KioProtocolMetadataTest)

#include "kioprotocolmetadatatest.moc"
