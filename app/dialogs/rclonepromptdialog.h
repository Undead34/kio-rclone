/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QDialog>
#include <QProcess>
#include <QStringList>

class QLineEdit;
class QPlainTextEdit;

/**
 * Hosts an rclone command which may ask questions on standard input.
 *
 * rclone owns the interaction protocol and decides which questions to ask for
 * each backend. This dialog only renders its transcript and sends the user's
 * answer back to its standard input, so it does not need provider-specific
 * prompt handling.
 */
class RclonePromptDialog final : public QDialog
{
public:
    RclonePromptDialog(const QString &program,
                            const QStringList &arguments,
                            const QString &title,
                            const QString &description,
                            QWidget *parent = nullptr);

    /// Starts rclone after the modal event loop is active and runs the dialog.
    int exec() override;

    [[nodiscard]] bool succeeded() const;
    [[nodiscard]] bool cancelled() const;
    [[nodiscard]] QString diagnostic() const;
    [[nodiscard]] QString startError() const;

protected:
    void reject() override;

private:
    void startProcess();
    void appendOutput(const QByteArray &output);
    void sendResponse();
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

    QProcess *m_process = nullptr;
    QPlainTextEdit *m_transcript = nullptr;
    QLineEdit *m_response = nullptr;
    QByteArray m_diagnostic;
    QString m_startError;
    bool m_started = false;
    bool m_succeeded = false;
    bool m_cancelled = false;
};
