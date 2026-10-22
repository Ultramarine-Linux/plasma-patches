// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

#ifndef UM_EXECUTABLE
#define UM_EXECUTABLE "/usr/bin/um"
#endif

/// Helpers shared by the Ultramarine KCMs for invoking the um CLI.
namespace UmCli {

    /// pkexec (started by um) exits with 126 when the authentication dialog is dismissed.
    inline constexpr int PkexecDismissedExitCode = 126;
    /// pkexec exits with 127 when authorization fails.
    inline constexpr int PkexecNotAuthorizedExitCode = 127;

    /**
     * Returns the absolute path to um, preferring the configured system path so the
     * polkit action shipped with umcli (which matches on exact path) applies.
     */
    inline QString executable() {
        if (auto configured = QStringLiteral(UM_EXECUTABLE); QFileInfo(configured).isExecutable()) {
            return configured;
        }
        return QStandardPaths::findExecutable(QStringLiteral("um"));
    }

    /**
     * The KCMs run um as the user. um escalates by itself when it needs root:
     * without a terminal it re-executes through pkexec (polkit prompt), and that
     * exit code is passed back to us. Running um unprivileged matters for
     * `upgrade apply`, which updates user-installation Flatpaks before escalating.
     *
     * Returns whether an exit code means the user cancelled or failed authentication.
     */
    inline bool isAuthenticationFailure(const int exitCode) {
        return exitCode == PkexecDismissedExitCode || exitCode == PkexecNotAuthorizedExitCode;
    }

} // namespace UmCli
