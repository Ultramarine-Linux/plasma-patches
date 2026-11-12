// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <chrono>
#include <cstdint>

namespace UltramarineUpdates {

    class InstallLock {
    public:
        enum class Result : std::uint8_t {
            Acquired,
            Busy,
            Failed,
        };

        explicit InstallLock(QString path) : m_path(std::move(path)) {}
        ~InstallLock();
        InstallLock(const InstallLock &) = delete;
        InstallLock &operator=(const InstallLock &) = delete;

        Result tryLock(QString *error = nullptr, std::chrono::milliseconds wait = {});

    private:
        QString m_path;
        int m_fd = -1;
    };

    QString systemLockPath();

    QString userLockPath();

    QString statusCacheLockPath();

    QString imageCheckLockPath();

    QString installBusyMessage();

} // namespace UltramarineUpdates
