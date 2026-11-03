// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
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
        /// the owner's uid
        uint uid = 0;
        bool locked = false;
        bool idle = false;
        /// when logind's IdleHint was last set, if it's known
        std::optional<QDateTime> idleSince;

        /// whether it's a `user` session of a graphical or text console type, so someone can sit at it
        [[nodiscard]] bool interactive() const;

        /// whether someone is at this session: an active, unlocked, non-idle `user`
        /// session of a graphical or text console type (wayland, x11, mir or tty)
        [[nodiscard]] bool inUse() const;
    };

    /// parses `loginctl show-session ID... -p ...` output: one block of key=value
    /// lines per session, separated by blank lines
    QList<LoginSession> parseShowSessions(const QString &output);

    /// returns the first session someone is using, optionally only sessions of @p userName
    std::optional<LoginSession> sessionInUse(const QList<LoginSession> &sessions, const QString &userName = {});

    /// lists the login sessions through loginctl
    bool loginSessions(QList<LoginSession> *sessions, QString *error);

    /// whether @p userName has an active (foreground) interactive session
    bool hasActiveSession(const QList<LoginSession> &sessions, const QString &userName);

    /**
     * since when every active interactive session (of @p userName, if set) has
     * been idle: the latest IdleSinceHint among them. Empty if one of them isn't
     * idle (e.g. only locked) or doesn't say since when, or if there are none.
     */
    std::optional<QDateTime> sessionsIdleSince(const QList<LoginSession> &sessions, const QString &userName = {});

    /// a logind inhibitor, as listed by org.freedesktop.login1.Manager.ListInhibitors
    struct Inhibitor {
        /// colon-separated, e.g. "sleep:idle"
        QString what;
        QString who;
        QString why;
        /// "block" or "delay"
        QString mode;
        uint uid = 0;
        uint pid = 0;
    };

    /**
     * whether someone keeps the device from going idle through logind, like a
     * video player using `systemd-inhibit --what=idle`: a non-root "idle" block
     * inhibitor of a user owning an interactive session (of @p userName, if set).
     * PowerDevil's own inhibitor and ours don't count.
     */
    bool idleInhibited(const QList<Inhibitor> &inhibitors, const QList<LoginSession> &sessions,
                       const QString &userName = {});

    /// lists logind's inhibitors over the system bus. Empty if it can't.
    QList<Inhibitor> logindInhibitors();

    /**
     * whether an app keeps the screen on through Plasma (org.freedesktop.ScreenSaver
     * or PowerDevil's PolicyAgent, which browsers and video players use). Asks
     * PowerDevil on the session bus, so it only works as the user. PowerDevil
     * doesn't pass these on to logind, and its IdleHint ignores them.
     */
    bool screenInhibitedInSession();

    /// where the user timer notes that screenInhibitedInSession() is true, so the
    /// system timer (as root, without the session bus) learns about it.
    /// $XDG_RUNTIME_DIR/ultramarine-updates/screen-inhibited for the current user.
    QString screenInhibitedMarkerPath();

    /// screenInhibitedMarkerPath() of the user with @p uid, as seen by root
    QString screenInhibitedMarkerPath(uint uid);

    /// records (or, if not @p inhibited, removes) the marker for the system timer
    void writeScreenInhibitedMarker(bool inhibited, const QDateTime &now);

    /**
     * whether @p path is a marker written less than @p maxAge before @p now by
     * @p uid. Root reads it from a directory that user controls, so anything but
     * a small regular file of theirs, in a directory of theirs, is ignored.
     * Symlinks aren't followed, and FIFOs and devices are never read (or opened,
     * unless swapped in at just the wrong moment, and then without blocking).
     */
    bool freshScreenInhibitedMarker(const QString &path, uint uid, std::chrono::seconds maxAge, const QDateTime &now);

    /// whether the device runs on AC power (also true without a battery)
    bool onACPower();

    /// parses seconds since the epoch. Empty if it isn't a number or out of QDateTime's range.
    std::optional<QDateTime> parseTimestamp(const QByteArray &text);

    std::optional<QDateTime> readTimestamp(const QString &path);
    bool writeTimestamp(const QString &path, const QDateTime &time, QString *error);

    /// whether the timestamp in @p path is less than @p duration before @p now.
    /// Timestamps from the future (clock changes) don't count.
    bool within(const QString &path, std::chrono::seconds duration, const QDateTime &now);

    /// how often the timers check whether the device is in use
    inline constexpr std::chrono::seconds pollInterval = std::chrono::minutes(5);

    /// longer between two polls, and no one watched whether the device was used in between
    inline constexpr std::chrono::seconds maxPollGap = 2 * pollInterval + std::chrono::minutes(1);

    /**
     * remembers since when no one has used the device. The timers poll every few
     * minutes, so "inactive for 5 minutes" means 5 minutes of consecutive polls
     * seeing no session in use.
     * A longer gap between polls (the device was suspended or off, or the clock
     * changed) starts counting again, since no one watched whether the device
     * was used then.
     */
    class ActivityTracker {
    public:
        /// keeps the start of inactivity in @p sincePath and the last poll in @p lastPollPath
        ActivityTracker(QString sincePath, QString lastPollPath,
                        std::chrono::seconds pollGap = UltramarineUpdates::maxPollGap) :
            m_sincePath(std::move(sincePath)), m_lastPollPath(std::move(lastPollPath)), m_maxPollGap(pollGap) {}

        /**
         * records the current observation and returns how long the device has
         * been inactive (0 if it's in use now). When inactivity starts, it counts
         * from @p idleSince (see sessionsIdleSince()) rather than from now, but
         * never from before the previous poll, which saw the device in use.
         */
        [[nodiscard]] std::chrono::seconds inactiveFor(bool inUse, const QDateTime &now,
                                                       const std::optional<QDateTime> &idleSince = {}) const;

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
