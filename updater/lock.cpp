// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "lock.h"

#include <KLocalizedString>

#include <QFile>
#include <QStandardPaths>
#include <QThread>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        QString describeError(const QString &path, const int errorNumber) {
            return u"%1: %2"_s.arg(path, QString::fromLocal8Bit(std::strerror(errorNumber)));
        }
    } // namespace

    InstallLock::~InstallLock() {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    InstallLock::Result InstallLock::tryLock(QString *error, const std::chrono::milliseconds wait) {
        if (m_fd >= 0) {
            return Result::Acquired;
        }
        constexpr mode_t ownerOnly = 0600;
        const int fd =
            open(QFile::encodeName(m_path).constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, ownerOnly);
        if (fd < 0) {
            if (error) {
                *error = describeError(m_path, errno);
            }
            return Result::Failed;
        }
        constexpr std::chrono::milliseconds retryDelay{100};
        std::chrono::milliseconds waited{0};
        int locked = flock(fd, LOCK_EX | LOCK_NB);
        while (locked != 0 && errno == EWOULDBLOCK && waited < wait) {
            QThread::sleep(retryDelay);
            waited += retryDelay;
            locked = flock(fd, LOCK_EX | LOCK_NB);
        }
        if (locked != 0) {
            const int lockError = errno;
            close(fd);
            if (lockError == EWOULDBLOCK) {
                return Result::Busy;
            }
            if (error) {
                *error = describeError(m_path, lockError);
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

    QString statusCacheLockPath() { return u"/run/ultramarine-updates-status.lock"_s; }

    QString imageCheckLockPath() { return u"/run/ultramarine-updates-image-check.lock"_s; }

    QString installBusyMessage() {
        return i18nc("@info:status",
                     "Updates are already being installed (automatic updates or Apply Tonight). Try "
                     "again once they're done.");
    }

} // namespace UltramarineUpdates
