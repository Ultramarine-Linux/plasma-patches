// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

/// Small helpers for running the package managers the updater drives.
namespace UltramarineUpdates {

    struct CommandResult {
        /// False if the program couldn't be started at all.
        bool started = false;
        /// The exit code, or -1 if the program crashed or didn't start.
        int exitCode = -1;
        QByteArray out;
        QByteArray err;

        [[nodiscard]] bool succeeded() const { return started && exitCode == 0; }
    };

    /// Runs @p program to completion and captures its output.
    CommandResult capture(const QString &program, const QStringList &arguments,
                          const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());

    /**
     * Runs @p program and returns its trimmed stdout. Exit codes in
     * @p allowedExitCodes count as success (`dnf check-update` exits with 100
     * when updates are available). On failure, returns false and sets @p error
     * to the program's stderr.
     */
    bool commandOutput(const QString &program, const QStringList &arguments, QString *output, QString *error,
                       const QList<int> &allowedExitCodes = {});

    /**
     * Runs a command with the C locale, so sizes and numbers aren't localized
     * (e.g. "1,1 GB"). Partial output is still useful (rpm -q with some packages
     * missing), so the exit code is ignored.
     */
    QString localeIndependentOutput(const QString &program, const QStringList &arguments);

    /**
     * Runs a command with our stdin, stdout and stderr, so download progress is
     * visible (in a terminal, or in the KCM's status line). Returns the exit
     * code, or -1 if it couldn't start or crashed.
     */
    int runForwarded(const QString &program, const QStringList &arguments);

    /// Returns the first executable among @p paths, falling back to PATH. Empty if none.
    QString findExecutable(const QString &name, const QStringList &paths = {});

    /// Returns the last non-empty line of @p text (nix puts the error last).
    QString lastLine(const QString &text);

} // namespace UltramarineUpdates
