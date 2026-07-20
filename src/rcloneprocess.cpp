/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneprocess.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace RcloneProcess
{
void appendLimited(QByteArray &destination, const QByteArray &data)
{
    const qsizetype remaining = MaximumDiagnosticSize - destination.size();
    if (remaining > 0) {
        destination.append(data.left(remaining));
    }
}

void configureProcess(QProcess &process, const QString &program, const QStringList &arguments)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
}

void stopProcess(QProcess &process)
{
    if (process.state() == QProcess::NotRunning) {
        return;
    }
    process.terminate();
    if (!process.waitForFinished(1000)) {
        process.kill();
        process.waitForFinished(1000);
    }
}

void collectProcessOutput(QProcess &process, QByteArray &standardOutput, QByteArray &standardError)
{
    appendLimited(standardOutput, process.readAllStandardOutput());
    appendLimited(standardError, process.readAllStandardError());
}
}
