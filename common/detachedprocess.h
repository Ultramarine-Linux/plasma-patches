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

    // ~QProcess would block System Settings for up to 30 seconds trying to kill a root process it can't kill
    inline void letFinishAfterModuleCloses(QProcess *process, const QObject *module) {
        process->disconnect(module);
        process->setParent(nullptr);
        QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
        QObject::connect(process, &QProcess::errorOccurred, process, [process](const QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                process->deleteLater();
            }
        });
    }

} // namespace DetachedProcess
