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

namespace UltramarineUpdates {

    struct ScheduleStatus {
        bool scheduled = false;
        qint64 next = 0;
        bool partial = false;

        bool operator==(const ScheduleStatus &) const = default;

        [[nodiscard]] QJsonObject toJson() const;
    };

    ScheduleStatus parseTimerShow(const QString &output);

    ScheduleStatus combineTonightStatus(const ScheduleStatus &system, const std::optional<ScheduleStatus> &user);

    ScheduleStatus tonightStatus();

    bool tonightAlreadyInstalled(const std::optional<QDateTime> &installed, const std::optional<QDateTime> &scheduled);

    bool readyToInstall(const Settings &settings, bool tonight, bool inUse, std::chrono::seconds inactive,
                        bool installedRecently, const QDateTime &now, QString *reason = nullptr);

    QString lastInstallPath(const QString &stateDir);

    void recordManualInstall(bool userManager, const QDateTime &now);

    bool installFromTimer(const QString &lockPath, const QString &stateDir, const QString &installedPath,
                          const QDateTime &now, const std::function<ApplyResult()> &install, bool *installed,
                          QString *error);

    void notifyPendingUpdates(const QString &checkedPath, const QDateTime &now,
                              const std::function<ListResponse()> &list, const std::function<void(qsizetype)> &notify);

    bool runScheduled(bool tonight, QString *error);

    bool runUser(bool tonight, QString *error);

    bool scheduleTonight(bool userManager, QString *error);

    bool cancelTonight(bool userManager, QString *error);

} // namespace UltramarineUpdates
