// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QProcess>
#include <QtGlobal>

#if QT_VERSION < QT_VERSION_CHECK(6, 7, 0)
#include <unistd.h>
#endif

namespace DetachedProcess {

    inline void detachFromTerminal(QProcess *process) {
        process->setStandardInputFile(QProcess::nullDevice());
        QProcess::UnixProcessFlags flags = QProcess::UnixProcessFlag::IgnoreSigPipe;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        flags |= QProcess::UnixProcessFlag::CreateNewSession;
#else
        process->setChildProcessModifier([] { ::setsid(); });
#endif
        process->setUnixProcessParameters(flags);
    }

} // namespace DetachedProcess
