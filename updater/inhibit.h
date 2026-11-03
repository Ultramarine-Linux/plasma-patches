// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

/// keeps the device from shutting down or sleeping while updates install
namespace UltramarineUpdates {

    /// the "who" of our inhibitors
    inline constexpr auto inhibitorName = "Ultramarine Updates";

    /**
     * a logind "shutdown:sleep" block inhibitor (org.freedesktop.login1.Manager.Inhibit),
     * held until destroyed. Installs go ahead without it if logind refuses (e.g.
     * polkit doesn't allow it outside an active session), so failing only warns.
     */
    class ShutdownInhibitor {
    public:
        /// @p why is shown by Plasma's logout screen and `systemd-inhibit --list`
        explicit ShutdownInhibitor(const QString &why);
        ~ShutdownInhibitor();
        ShutdownInhibitor(const ShutdownInhibitor &) = delete;
        ShutdownInhibitor &operator=(const ShutdownInhibitor &) = delete;

        [[nodiscard]] bool held() const { return m_fd >= 0; }

    private:
        int m_fd = -1;
    };

} // namespace UltramarineUpdates
