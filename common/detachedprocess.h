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

    /**
     * starts @p process without stdin and in a new session, so it has no controlling terminal.
     * um and pkexec prompt on a terminal when they have one (um through sudo), where nobody
     * sees it if System Settings was started from one. SIGPIPE is ignored too, which QProcess
     * would otherwise reset to the default (exit) in the child.
     *
     * The child leads its own session and process group, so its group ID is its PID.
     */
    inline void detachFromTerminal(QProcess *process) {
        process->setStandardInputFile(QProcess::nullDevice());
        QProcess::UnixProcessFlags flags = QProcess::UnixProcessFlag::IgnoreSigPipe;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        flags |= QProcess::UnixProcessFlag::CreateNewSession;
#else
        // CreateNewSession is new in Qt 6.7. setsid() is async-signal-safe, so it can run
        // between fork and exec.
        process->setChildProcessModifier([] { ::setsid(); });
#endif
        process->setUnixProcessParameters(flags);
    }

} // namespace DetachedProcess
