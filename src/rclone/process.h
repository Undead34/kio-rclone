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

/// Applies the project-wide safe process environment and argument setup. High
/// level code supplies rclone subcommands; it must not duplicate this setup.
void configureProcess(QProcess &process, const QString &program, const QStringList &arguments);

/// Terminates a child process promptly and drains it without assuming a Qt
/// event loop exists in the KIO worker.
void stopProcess(QProcess &process);

/// Collects bounded diagnostics so failed rclone invocations remain useful but
/// cannot grow worker memory without limit.
void collectProcessOutput(QProcess &process, QByteArray &standardOutput, QByteArray &standardError);
}
