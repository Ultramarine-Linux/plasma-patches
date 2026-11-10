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

// tests point this at a fake um. kcm_common already sets UM_EXECUTABLE for every target, so
// a second -D for it would only clash.
#ifdef UM_TEST_EXECUTABLE
#undef UM_EXECUTABLE
#define UM_EXECUTABLE UM_TEST_EXECUTABLE
#endif

/// helpers for invoking the um CLI (Tweaks KCM)
namespace UmCli {

    /**
     * returns the absolute path to um, preferring the configured system path so the
     * polkit action shipped with umcli (which matches on exact path) applies
     */
    inline QString executable() {
        if (auto configured = QStringLiteral(UM_EXECUTABLE); QFileInfo(configured).isExecutable()) {
            return configured;
        }
        return QStandardPaths::findExecutable(QStringLiteral("um"));
    }

    /// um escalates through pkexec by itself (see Pkexec)
    using Pkexec::isAuthenticationFailure;

} // namespace UmCli
