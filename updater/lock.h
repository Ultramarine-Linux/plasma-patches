// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

/// keeps automatic updates, Apply Tonight and manual installs from running at the same time
namespace UltramarineUpdates {

    /**
     * an exclusive, non-blocking flock(2) on a lock file, held until destroyed
     * one lock covers the system's updates (as root), another each user's
     */
    class InstallLock {
    public:
        enum class Result {
            Acquired,
            /// another install holds the lock
            Busy,
            /// the lock file couldn't be opened
            Failed,
        };

        explicit InstallLock(QString path) : m_path(std::move(path)) {}
        ~InstallLock();
        InstallLock(const InstallLock &) = delete;
        InstallLock &operator=(const InstallLock &) = delete;

        Result tryLock(QString *error = nullptr);

    private:
        QString m_path;
        int m_fd = -1;
    };

    /// /run/ultramarine-updates.lock: system updates, as root
    QString systemLockPath();

    /// $XDG_RUNTIME_DIR/ultramarine-updates.lock: the user's own updates
    QString userLockPath();

    /// what to tell someone installing by hand while another install runs
    QString installBusyMessage();

} // namespace UltramarineUpdates
