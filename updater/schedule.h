// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <chrono>

#include "settings.h"

/**
 * automatic updates and Apply Tonight.
 *
 * The helper's timers call runScheduled() (system, as root) and runUser() (per
 * user, started by the XDG autostart entry) every few minutes. They track how
 * long no one has used the device and install at most once a day. Otherwise
 * they quietly do nothing. With automatic updates off, runUser() notifies about
 * pending updates once a day instead.
 *
 * scheduleTonight() ("Apply Tonight") creates transient timers that do the same
 * once, tonight, even if automatic updates are off. The device is never
 * restarted automatically.
 */
namespace UltramarineUpdates {

    /// the state of Apply Tonight
    struct ScheduleStatus {
        bool scheduled = false;
        /// when tonight's run is next attempted, as a Unix timestamp (0 if unknown)
        qint64 next = 0;

        bool operator==(const ScheduleStatus &) const = default;

        [[nodiscard]] QJsonObject toJson() const;
    };

    /// parses `systemctl show --timestamp=unix -p ActiveState -p NextElapseUSecRealtime`
    ScheduleStatus parseTimerShow(const QString &output);

    /// whether Apply Tonight is scheduled in the system manager
    ScheduleStatus tonightStatus();

    /// decides whether a timer run should install updates now. Sets @p reason
    /// when it shouldn't. Separate from the side effects so it can be tested.
    bool readyToInstall(const Settings &settings, bool tonight, bool inUse, std::chrono::seconds inactive,
                        bool installedRecently, const QDateTime &now, QString *reason = nullptr);

    /// installs system updates from the automatic or tonight timer. Runs as root.
    bool runScheduled(bool tonight, QString *error);

    /// installs the user's own updates when automatic (or tonight's) updates
    /// apply, otherwise notifies about pending updates once a day. Runs as the user.
    bool runUser(bool tonight, QString *error);

    /// sets up Apply Tonight in the user's manager (@p userManager) or the system
    /// manager (as root). Scheduling again replaces the old timer.
    bool scheduleTonight(bool userManager, QString *error);

    /// removes Apply Tonight. It's not an error if nothing was scheduled. Returns false
    /// and sets @p error if the timer couldn't be stopped.
    bool cancelTonight(bool userManager, QString *error);

} // namespace UltramarineUpdates
