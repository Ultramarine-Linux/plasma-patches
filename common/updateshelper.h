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

namespace UpdatesHelper {

    // polkit actions match on the exact path, so prefer the configured one
    inline QString executable() {
        if (auto configured = QStringLiteral(ULTRAMARINE_UPDATES_EXECUTABLE); QFileInfo(configured).isExecutable()) {
            return configured;
        }
        return QStandardPaths::findExecutable(QStringLiteral("ultramarine-updates"));
    }

} // namespace UpdatesHelper
