// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

/**
 * The KCMs run their backends (um, ultramarine-updates) as the user. A backend
 * escalates by itself when it needs root: without a terminal it re-executes
 * through pkexec (polkit prompt), and pkexec's exit code is passed back to us.
 */
namespace Pkexec {

    /// pkexec exits with 126 when the authentication dialog is dismissed.
    inline constexpr int DismissedExitCode = 126;
    /// pkexec exits with 127 when authorization fails.
    inline constexpr int NotAuthorizedExitCode = 127;

    /// Returns whether an exit code means the user cancelled or failed authentication.
    inline bool isAuthenticationFailure(const int exitCode) {
        return exitCode == DismissedExitCode || exitCode == NotAuthorizedExitCode;
    }

} // namespace Pkexec
