/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dialogs/rclonepromptdialog.h"

#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTimer>

#include <QtTest>

class RclonePromptDialogTest final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void handlesImmediateCompletion();
    void forwardsResponseToChildProcess();
};

void RclonePromptDialogTest::handlesImmediateCompletion()
{
    RclonePromptDialog dialog(QStringLiteral("/bin/sh"),
                                   {QStringLiteral("-c"), QStringLiteral("exit 0")},
                                   QStringLiteral("Test"),
                                   QStringLiteral("Test"));

    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &dialog, [&dialog]() {
        static_cast<QDialog &>(dialog).reject();
    });
    timeout.start(5000);

    QCOMPARE(dialog.exec(), int(QDialog::Accepted));
    timeout.stop();
    QVERIFY(dialog.succeeded());
}

void RclonePromptDialogTest::forwardsResponseToChildProcess()
{
    const QString script = QStringLiteral("printf 'Prompt> '; IFS= read -r answer; "
                                          "printf '\\nreceived:%s\\n' \"$answer\"; test \"$answer\" = n");
    RclonePromptDialog dialog(QStringLiteral("/bin/sh"),
                                   {QStringLiteral("-c"), script},
                                   QStringLiteral("Test"),
                                   QStringLiteral("Test"));

    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &dialog, [&dialog]() {
        static_cast<QDialog &>(dialog).reject();
    });
    timeout.start(5000);

    bool sentResponse = false;
    QTimer::singleShot(100, &dialog, [&dialog, &sentResponse]() {
        auto *response = dialog.findChild<QLineEdit *>();
        if (!response) {
            static_cast<QDialog &>(dialog).reject();
            return;
        }
        sentResponse = true;
        response->setText(QStringLiteral("n"));
        response->returnPressed();
    });

    QCOMPARE(dialog.exec(), int(QDialog::Accepted));
    timeout.stop();
    QVERIFY(sentResponse);
    QVERIFY(dialog.succeeded());

    const auto *transcript = dialog.findChild<QPlainTextEdit *>();
    QVERIFY(transcript);
    QVERIFY(transcript->toPlainText().contains(QStringLiteral("Prompt>")));
    QVERIFY(transcript->toPlainText().contains(QStringLiteral("received:n")));
}

QTEST_MAIN(RclonePromptDialogTest)

#include "rclonepromptdialogtest.moc"
