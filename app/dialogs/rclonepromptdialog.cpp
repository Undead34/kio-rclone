/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rclonepromptdialog.h"

#include "appid.h"
#include "rclone/process.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

RclonePromptDialog::RclonePromptDialog(const QString &program,
                                                 const QStringList &arguments,
                                                 const QString &title,
                                                 const QString &description,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_process(new QProcess(this))
{
    setWindowTitle(title);
    setWindowIcon(QIcon::fromTheme(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID)));
    setMinimumWidth(560);
    resize(620, 420);

    auto *layout = new QVBoxLayout(this);

    auto *descriptionLabel = new QLabel(description, this);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);

    auto *transcriptLabel = new QLabel(i18n("rclone output:"), this);
    layout->addWidget(transcriptLabel);

    m_transcript = new QPlainTextEdit(this);
    m_transcript->setReadOnly(true);
    m_transcript->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_transcript->setAccessibleName(i18n("rclone output"));
    transcriptLabel->setBuddy(m_transcript);
    layout->addWidget(m_transcript, 1);

    auto *responseLayout = new QFormLayout;
    m_response = new QLineEdit(this);
    m_response->setPlaceholderText(i18n("Type a response and press Enter"));
    m_response->setAccessibleDescription(i18n("Sends your response to the question shown by rclone."));
    responseLayout->addRow(i18n("Response:"), m_response);
    layout->addLayout(responseLayout);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    RcloneProcess::configureProcess(*m_process, program, arguments);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }

        m_startError = m_process->errorString();
        QDialog::reject();
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &RclonePromptDialog::processFinished);
    connect(m_response, &QLineEdit::returnPressed, this, &RclonePromptDialog::sendResponse);
    connect(buttons, &QDialogButtonBox::rejected, this, &RclonePromptDialog::reject);
}

int RclonePromptDialog::exec()
{
    QTimer::singleShot(0, this, &RclonePromptDialog::startProcess);
    return QDialog::exec();
}

void RclonePromptDialog::startProcess()
{
    if (m_started) {
        return;
    }

    m_started = true;
    m_process->start();
    m_response->setFocus();
}

bool RclonePromptDialog::succeeded() const
{
    return m_succeeded;
}

bool RclonePromptDialog::cancelled() const
{
    return m_cancelled;
}

QString RclonePromptDialog::diagnostic() const
{
    return QString::fromUtf8(m_diagnostic).trimmed();
}

QString RclonePromptDialog::startError() const
{
    return m_startError;
}

void RclonePromptDialog::reject()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_cancelled = true;
        RcloneProcess::stopProcess(*m_process);
    }
    QDialog::reject();
}

void RclonePromptDialog::appendOutput(const QByteArray &output)
{
    if (output.isEmpty()) {
        return;
    }

    RcloneProcess::appendLimited(m_diagnostic, output);

    QTextCursor cursor = m_transcript->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QString::fromUtf8(output));
    m_transcript->setTextCursor(cursor);
    m_transcript->ensureCursorVisible();
}

void RclonePromptDialog::sendResponse()
{
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_process->write(m_response->text().toUtf8());
    m_process->write("\n");
    m_response->clear();
}

void RclonePromptDialog::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendOutput(m_process->readAllStandardOutput());
    m_succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
    m_response->setEnabled(false);

    if (m_cancelled) {
        return;
    }

    m_succeeded ? QDialog::accept() : QDialog::reject();
}
