// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <chrono>
#include <cstdint>

/// keeps automatic updates, Apply Tonight and manual installs from running at the same time
namespace UltramarineUpdates {

    /**
     * an exclusive, non-blocking flock(2) on a lock file, held until destroyed.
     * One lock covers the system's updates (as root), another each user's.
     */
    class InstallLock {
    public:
        enum class Result : std::uint8_t {
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

        /**
         * takes the lock without blocking. While another process holds it, retries
         * for up to @p wait before reporting Busy, for holders that only keep it
         * for a moment.
         */
        Result tryLock(QString *error = nullptr, std::chrono::milliseconds wait = {});

    private:
        QString m_path;
        int m_fd = -1;
    };

    /// /run/ultramarine-updates.lock: system updates, as root
    QString systemLockPath();

    /// $XDG_RUNTIME_DIR/ultramarine-updates.lock: the user's own updates
    QString userLockPath();

    /**
     * /run/ultramarine-updates-status.lock: writing the bootc status cache, as root.
     * Separate from the system lock, so refreshing the cache never turns away an install.
     */
    QString statusCacheLockPath();

    /// what to tell someone installing by hand while another install runs
    QString installBusyMessage();

} // namespace UltramarineUpdates
