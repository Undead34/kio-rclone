/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QStringList>

class QProcess;

namespace RcloneProcess
{
// Upper bound on the diagnostic output captured from a process. Both the
// standard output and error streams are truncated to this size.
constexpr qsizetype MaximumDiagnosticSize = 64 * 1024;

// Appends up to MaximumDiagnosticSize bytes to destination, dropping the rest.
void appendLimited(QByteArray &destination, const QByteArray &data);

void configureProcess(QProcess &process, const QString &program, const QStringList &arguments);
void stopProcess(QProcess &process);
void collectProcessOutput(QProcess &process, QByteArray &standardOutput, QByteArray &standardError);
}
