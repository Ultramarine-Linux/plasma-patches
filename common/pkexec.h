// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace Pkexec {

    inline constexpr int DismissedExitCode = 126;
    inline constexpr int NotAuthorizedExitCode = 127;

    inline bool isAuthenticationFailure(const int exitCode) {
        return exitCode == DismissedExitCode || exitCode == NotAuthorizedExitCode;
    }

} // namespace Pkexec
