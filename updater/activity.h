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

namespace UltramarineUpdates {

    struct LoginSession {
        QString id;
        QString name;
        QString sessionClass;
        QString type;
        bool active = false;
        uint uid = 0;
        bool locked = false;
        bool idle = false;
        std::optional<QDateTime> idleSince;

        [[nodiscard]] bool interactive() const;

        [[nodiscard]] bool inUse() const;
    };

    QList<LoginSession> parseShowSessions(const QString &output);

    std::optional<LoginSession> sessionInUse(const QList<LoginSession> &sessions, const QString &userName = {});

    bool loginSessions(QList<LoginSession> *sessions, QString *error);

    bool hasActiveSession(const QList<LoginSession> &sessions, const QString &userName);

    std::optional<QDateTime> sessionsIdleSince(const QList<LoginSession> &sessions, const QString &userName = {});

    struct Inhibitor {
        QString what;
        QString who;
        QString why;
        QString mode;
        uint uid = 0;
        uint pid = 0;
    };

    bool idleInhibited(const QList<Inhibitor> &inhibitors, const QList<LoginSession> &sessions,
                       const QString &userName = {});

    QList<Inhibitor> logindInhibitors();

    bool screenInhibitedInSession();

    QString screenInhibitedMarkerPath();

    QString screenInhibitedMarkerPath(uint uid);

    void writeScreenInhibitedMarker(bool inhibited, const QDateTime &now);

    bool freshScreenInhibitedMarker(const QString &path, uint uid, std::chrono::seconds maxAge, const QDateTime &now);

    bool onACPower();

    std::optional<QDateTime> parseTimestamp(const QByteArray &text);

    std::optional<QDateTime> readTimestamp(const QString &path);
    bool writeTimestamp(const QString &path, const QDateTime &time, QString *error);

    bool within(const QString &path, std::chrono::seconds duration, const QDateTime &now);

    inline constexpr std::chrono::seconds pollInterval = std::chrono::minutes(5);

    inline constexpr std::chrono::seconds maxPollGap = 2 * pollInterval + std::chrono::minutes(1);

    class ActivityTracker {
    public:
        ActivityTracker(QString sincePath, QString lastPollPath,
                        const std::chrono::seconds maxGapBetweenPolls = maxPollGap) :
            m_sincePath(std::move(sincePath)), m_lastPollPath(std::move(lastPollPath)),
            m_maxPollGap(maxGapBetweenPolls) {}

        [[nodiscard]] std::chrono::seconds inactiveFor(bool inUse, const QDateTime &now,
                                                       const std::optional<QDateTime> &idleSince = {}) const;

    private:
        QString m_sincePath;
        QString m_lastPollPath;
        std::chrono::seconds m_maxPollGap;
    };

    QString systemStateDir();

    QString userStateDir();

} // namespace UltramarineUpdates
