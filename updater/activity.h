// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <chrono>
#include <optional>

/// whether someone is using the device, and timestamps the timers keep
namespace UltramarineUpdates {

    struct LoginSession {
        QString id;
        QString name;
        QString sessionClass;
        QString type;
        bool active = false;
        bool locked = false;
        bool idle = false;

        /// whether someone is at this session: an active, unlocked, non-idle login
        /// session on a seat (graphical or text console)
        [[nodiscard]] bool inUse() const;
    };

    /// parses `loginctl show-session ID... -p ...` output: one block of key=value
    /// lines per session, separated by blank lines
    QList<LoginSession> parseShowSessions(const QString &output);

    /// returns the first session someone is using, optionally only sessions of @p userName
    std::optional<LoginSession> sessionInUse(const QList<LoginSession> &sessions, const QString &userName = {});

    /// lists the login sessions through loginctl
    bool loginSessions(QList<LoginSession> *sessions, QString *error);

    /// whether the device runs on AC power (also true without a battery)
    bool onACPower();

    std::optional<QDateTime> readTimestamp(const QString &path);
    bool writeTimestamp(const QString &path, const QDateTime &time, QString *error);

    /// whether the timestamp in @p path is less than @p duration before @p now.
    /// Timestamps from the future (clock changes) don't count.
    bool within(const QString &path, std::chrono::seconds duration, const QDateTime &now);

    /// how often the timers check whether the device is in use
    inline constexpr std::chrono::seconds pollInterval = std::chrono::minutes(5);

    /**
     * remembers since when no one has used the device. The timers poll every few
     * minutes, so "inactive for 30 minutes" means 30 minutes of consecutive polls
     * seeing no session in use.
     * A longer gap between polls (the device was suspended or off, or the clock
     * changed) starts counting again, since no one watched whether the device
     * was used then.
     */
    class ActivityTracker {
    public:
        /// keeps the start of inactivity in @p sincePath and the last poll in @p lastPollPath
        ActivityTracker(QString sincePath, QString lastPollPath,
                        std::chrono::seconds maxPollGap = 2 * pollInterval + std::chrono::minutes(1)) :
            m_sincePath(std::move(sincePath)), m_lastPollPath(std::move(lastPollPath)), m_maxPollGap(maxPollGap) {}

        /// records the current observation and returns how long the device has
        /// been inactive (0 if it's in use now)
        [[nodiscard]] std::chrono::seconds inactiveFor(bool inUse, const QDateTime &now) const;

    private:
        QString m_sincePath;
        QString m_lastPollPath;
        std::chrono::seconds m_maxPollGap;
    };

    /// /var/lib/ultramarine-updates, or $ULTRAMARINE_UPDATES_STATE
    QString systemStateDir();

    /// $XDG_STATE_HOME/ultramarine-updates (~/.local/state/ultramarine-updates)
    QString userStateDir();

} // namespace UltramarineUpdates
