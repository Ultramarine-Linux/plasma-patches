// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

#include "pkexec.h"

#ifndef UM_EXECUTABLE
#define UM_EXECUTABLE "/usr/bin/um"
#endif

/// Helpers for invoking the um CLI (Tweaks KCM).
namespace UmCli {

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

    /// um escalates through pkexec by itself; see Pkexec.
    using Pkexec::isAuthenticationFailure;

} // namespace UmCli
