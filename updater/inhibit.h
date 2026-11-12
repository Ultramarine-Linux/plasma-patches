// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

namespace UltramarineUpdates {

    inline constexpr auto inhibitorName = "Ultramarine Updates";

    class ShutdownInhibitor {
    public:
        explicit ShutdownInhibitor(const QString &why);
        ~ShutdownInhibitor();
        ShutdownInhibitor(const ShutdownInhibitor &) = delete;
        ShutdownInhibitor &operator=(const ShutdownInhibitor &) = delete;

        [[nodiscard]] bool held() const { return m_fd >= 0; }

    private:
        int m_fd = -1;
    };

} // namespace UltramarineUpdates
