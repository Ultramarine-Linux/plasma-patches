// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "lock.h"

#include <QFile>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    InstallLock::~InstallLock() {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    InstallLock::Result InstallLock::tryLock(QString *error) {
        if (m_fd >= 0) {
            return Result::Acquired;
        }
        // Private, so other users can't hold the system's lock to stop its updates.
        // Close-on-exec, so package managers we start don't keep it after we exit.
        const int fd = open(QFile::encodeName(m_path).constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) {
            if (error) {
                *error = u"%1: %2"_s.arg(m_path, QString::fromLocal8Bit(std::strerror(errno)));
            }
            return Result::Failed;
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            const int lockError = errno;
            close(fd);
            if (lockError == EWOULDBLOCK) {
                return Result::Busy;
            }
            if (error) {
                *error = u"%1: %2"_s.arg(m_path, QString::fromLocal8Bit(std::strerror(lockError)));
            }
            return Result::Failed;
        }
        m_fd = fd;
        return Result::Acquired;
    }

    QString systemLockPath() { return u"/run/ultramarine-updates.lock"_s; }

    QString userLockPath() {
        QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtimeDir.isEmpty()) {
            runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        }
        return runtimeDir + "/ultramarine-updates.lock"_L1;
    }

    QString installBusyMessage() {
        return u"updates are already being installed (automatic updates or Apply Tonight); try again once they're done"_s;
    }

} // namespace UltramarineUpdates
