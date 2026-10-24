// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

/**
 * System-wide automatic update settings. They decide whether updates install by
 * themselves ("Let your device decide") or only notify ("Ask me"), how long the
 * device must be unused first, an optional time window, and whether RPM updates
 * are staged for the next restart.
 */
namespace UltramarineUpdates {

    struct Settings {
        bool automatic = true;
        bool offline = false;
        int inactiveMinutes = 30;
        /// "HH:MM", both empty for any time.
        QString windowStart;
        QString windowEnd;

        bool operator==(const Settings &) const = default;

        /// Checks values that can't be expressed in the JSON types. Empty if valid.
        [[nodiscard]] QString validate() const;

        [[nodiscard]] QJsonObject toJson() const;
    };

    /// Parses "HH:MM" into minutes after midnight.
    std::optional<int> parseClock(const QString &clock);

    /// Whether @p now falls between start and end ("HH:MM"). The window may wrap
    /// past midnight (22:00-06:00). An empty window is always open.
    bool inWindow(const QDateTime &now, const QString &start, const QString &end);

    /// Parses a window like "01:00-05:00". An empty string means any time.
    bool parseWindow(const QString &window, QString *start, QString *end, QString *error);

    /// /etc/ultramarine-updates/settings.json, or $ULTRAMARINE_UPDATES_CONFIG.
    QString settingsPath();

    /// Parses settings JSON. Missing keys keep their defaults.
    bool parseSettings(const QByteArray &json, Settings *settings, QString *error);

    /// Reads the settings, falling back to the defaults for a missing file. On
    /// error, @p settings holds the defaults.
    bool loadSettings(Settings *settings, QString *error);

    /// Writes the settings world-readable, so the KCM and user timers can read them.
    bool saveSettings(const Settings &settings, QString *error);

} // namespace UltramarineUpdates
