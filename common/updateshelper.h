// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

#include "pkexec.h"

#ifndef ULTRAMARINE_UPDATES_EXECUTABLE
#define ULTRAMARINE_UPDATES_EXECUTABLE "/usr/libexec/ultramarine-updates"
#endif

/// The update helper (updater/) behind the Updates KCM.
namespace UpdatesHelper {

    /**
     * Returns the absolute path to the helper, preferring the configured install
     * path so its polkit action (which matches on the exact path) applies.
     */
    inline QString executable() {
        if (auto configured = QStringLiteral(ULTRAMARINE_UPDATES_EXECUTABLE); QFileInfo(configured).isExecutable()) {
            return configured;
        }
        return QStandardPaths::findExecutable(QStringLiteral("ultramarine-updates"));
    }

} // namespace UpdatesHelper
