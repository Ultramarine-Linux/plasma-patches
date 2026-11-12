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

#ifdef UM_TEST_EXECUTABLE
#undef UM_EXECUTABLE
#define UM_EXECUTABLE UM_TEST_EXECUTABLE
#endif

namespace UmCli {

    // polkit actions match on the exact path, so prefer the configured one
    inline QString executable() {
        if (auto configured = QStringLiteral(UM_EXECUTABLE); QFileInfo(configured).isExecutable()) {
            return configured;
        }
        return QStandardPaths::findExecutable(QStringLiteral("um"));
    }

    using Pkexec::isAuthenticationFailure;

} // namespace UmCli
