// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>

#include <chrono>
#include <functional>
#include <optional>

#include "settings.h"
#include "updates.h"

/**
 * automatic updates and Apply Tonight.
 *
 * The helper's timers call runScheduled() (system, as root) and runUser() (per
 * user, started by the XDG autostart entry) every few minutes. They track how
 * long no one has used the device and install at most once every 20 hours.
 * Otherwise they quietly do nothing. With automatic updates off, runUser()
 * notifies about pending updates once a day instead.
 *
 * scheduleTonight() ("Apply Tonight") creates transient timers that do the same
 * once, tonight, even if automatic updates are off. They stop without
 * installing if automatic updates already installed since. The device is never
 * restarted automatically.
 */
namespace UltramarineUpdates {

    /// the state of Apply Tonight
    struct ScheduleStatus {
        /// whether every part of Apply Tonight is scheduled
        bool scheduled = false;
        /// when tonight's run is next attempted, as a Unix timestamp (0 if unknown)
        qint64 next = 0;
        /// whether only the system's or only the user's timer is still scheduled,
        /// e.g. after logging out stopped the user's
        bool partial = false;

        bool operator==(const ScheduleStatus &) const = default;

        [[nodiscard]] QJsonObject toJson() const;
    };

    /// parses `systemctl show --timestamp=unix -p ActiveState -p NextElapseUSecRealtime`
    ScheduleStatus parseTimerShow(const QString &output);

    /// combines the system timer's and (when run as the user) the user timer's status
    ScheduleStatus combineTonightStatus(const ScheduleStatus &system, const std::optional<ScheduleStatus> &user);

    /// whether Apply Tonight is scheduled: in the system manager, and also in the
    /// user's manager unless run as root
    ScheduleStatus tonightStatus();

    /// whether updates were @p installed (by the automatic timer) since Apply
    /// Tonight was @p scheduled, so tonight's timer has nothing left to do
    bool tonightAlreadyInstalled(const std::optional<QDateTime> &installed, const std::optional<QDateTime> &scheduled);

    /// decides whether a timer run should install updates now. Sets @p reason
    /// when it shouldn't. Separate from the side effects so it can be tested.
    bool readyToInstall(const Settings &settings, bool tonight, bool inUse, std::chrono::seconds inactive,
                        bool installedRecently, const QDateTime &now, QString *reason = nullptr);

    /**
     * runs @p install for a timer once it's ready, unless an install is already
     * running (that's not an error) or the last failed one was less than an hour
     * ago: tonight's timers poll every 5 minutes, and a failure shouldn't retry
     * all night. Once ApplyResult::installed() (even with some sources failing,
     * which are then reported in @p error), remembers the time in @p installedPath
     * and sets @p installed, so the timers wait for their next regular run.
     * Otherwise it's retried after an hour. Returns false if anything failed.
     */
    bool installFromTimer(const QString &lockPath, const QString &stateDir, const QString &installedPath,
                          const QDateTime &now, const std::function<ApplyResult()> &install, bool *installed,
                          QString *error);

    /**
     * "Ask me": lists updates (@p list) at most once a day and passes how many
     * there are to @p notify, if any. The check is remembered in @p checkedPath
     * only once a listing worked, so a failed one (no network yet) is retried on
     * the next run.
     */
    void notifyPendingUpdates(const QString &checkedPath, const QDateTime &now,
                              const std::function<ListResponse()> &list, const std::function<void(qsizetype)> &notify);

    /// installs system updates from the automatic or tonight timer. Runs as root.
    bool runScheduled(bool tonight, QString *error);

    /// installs the user's own updates when automatic (or tonight's) updates
    /// apply, otherwise notifies about pending updates once a day. Runs as the user.
    bool runUser(bool tonight, QString *error);

    /// sets up Apply Tonight in the user's manager (@p userManager) or the system
    /// manager (as root). Scheduling again replaces the old timer, unless tonight's
    /// updates are installing right now.
    bool scheduleTonight(bool userManager, QString *error);

    /// removes Apply Tonight. It's not an error if nothing was scheduled. Returns false
    /// and sets @p error if the timer couldn't be stopped.
    bool cancelTonight(bool userManager, QString *error);

} // namespace UltramarineUpdates
