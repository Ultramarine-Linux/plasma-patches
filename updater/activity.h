// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <chrono>
#include <optional>

/// Whether someone is using the device, and timestamps the timers keep.
namespace UltramarineUpdates {

    struct LoginSession {
        QString id;
        QString name;
        QString sessionClass;
        QString type;
        bool active = false;
        bool locked = false;
        bool idle = false;

        /// Whether someone is at this session: an active, unlocked, non-idle login
        /// session on a seat (graphical or text console).
        [[nodiscard]] bool inUse() const;
    };

    /// Parses `loginctl show-session ID... -p ...` output: one block of key=value
    /// lines per session, separated by blank lines.
    QList<LoginSession> parseShowSessions(const QString &output);

    /// Returns the first session someone is using, optionally only sessions of @p userName.
    std::optional<LoginSession> sessionInUse(const QList<LoginSession> &sessions, const QString &userName = {});

    /// Lists the login sessions through loginctl.
    bool loginSessions(QList<LoginSession> *sessions, QString *error);

    /// Whether the device runs on AC power (also true without a battery).
    bool onACPower();

    std::optional<QDateTime> readTimestamp(const QString &path);
    bool writeTimestamp(const QString &path, const QDateTime &time, QString *error);

    /// Whether the timestamp in @p path is less than @p duration before @p now.
    /// Timestamps from the future (clock changes) don't count.
    bool within(const QString &path, std::chrono::seconds duration, const QDateTime &now);

    /**
     * Remembers since when no one has used the device. The timers poll every few
     * minutes, so "inactive for 30 minutes" means 30 minutes of consecutive polls
     * seeing no session in use.
     */
    class ActivityTracker {
    public:
        explicit ActivityTracker(QString path) : m_path(std::move(path)) {}

        /// Records the current observation and returns how long the device has
        /// been inactive (0 if it's in use now).
        [[nodiscard]] std::chrono::seconds inactiveFor(bool inUse, const QDateTime &now) const;

    private:
        QString m_path;
    };

    /// /var/lib/ultramarine-updates, or $ULTRAMARINE_UPDATES_STATE.
    QString systemStateDir();

    /// $XDG_STATE_HOME/ultramarine-updates (~/.local/state/ultramarine-updates).
    QString userStateDir();

} // namespace UltramarineUpdates
