// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace UltramarineUpdates {

    struct Settings {
        bool automatic = true;
        bool offline = false;
        int inactiveMinutes = 5;
        QString windowStart;
        QString windowEnd;

        bool operator==(const Settings &) const = default;

        [[nodiscard]] QString validate() const;

        [[nodiscard]] QJsonObject toJson() const;
    };

    std::optional<int> parseClock(const QString &clock);

    bool inWindow(const QDateTime &now, const QString &start, const QString &end);

    bool parseWindow(const QString &window, QString *start, QString *end, QString *error);

    QString settingsPath();

    bool parseSettings(const QByteArray &json, Settings *settings, QString *error);

    bool loadSettings(Settings *settings, QString *error);

    Settings loadSettingsOrDefaults(QString *warning);

    bool saveSettings(const Settings &settings, QString *error);

} // namespace UltramarineUpdates
