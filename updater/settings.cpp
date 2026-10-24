// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        constexpr int maxInactiveMinutes = 24 * 60;
    } // namespace

    QString Settings::validate() const {
        if (inactiveMinutes < 0 || inactiveMinutes > maxInactiveMinutes) {
            return u"inactive minutes must be between 0 and %1"_s.arg(maxInactiveMinutes);
        }
        if (windowStart.isEmpty() != windowEnd.isEmpty()) {
            return u"a time window needs both a start and an end"_s;
        }
        for (const QString &clock: {windowStart, windowEnd}) {
            if (!clock.isEmpty() && !parseClock(clock)) {
                return u"invalid time \"%1\", expected HH:MM"_s.arg(clock);
            }
        }
        return {};
    }

    QJsonObject Settings::toJson() const {
        return {
            {u"automatic"_s, automatic},      {u"offline"_s, offline},      {u"inactive_minutes"_s, inactiveMinutes},
            {u"window_start"_s, windowStart}, {u"window_end"_s, windowEnd},
        };
    }

    std::optional<int> parseClock(const QString &clock) {
        static const QRegularExpression pattern(u"^(\\d{1,2}):(\\d{2})$"_s);
        const QRegularExpressionMatch match = pattern.match(clock);
        if (!match.hasMatch()) {
            return std::nullopt;
        }
        const int hour = match.captured(1).toInt();
        const int minute = match.captured(2).toInt();
        if (hour > 23 || minute > 59) {
            return std::nullopt;
        }
        return hour * 60 + minute;
    }

    bool inWindow(const QDateTime &now, const QString &start, const QString &end) {
        if (start.isEmpty() || end.isEmpty()) {
            return true;
        }
        const std::optional<int> from = parseClock(start);
        const std::optional<int> to = parseClock(end);
        if (!from || !to) {
            return true;
        }
        const int minute = now.time().hour() * 60 + now.time().minute();
        if (*from <= *to) {
            return minute >= *from && minute < *to;
        }
        return minute >= *from || minute < *to;
    }

    bool parseWindow(const QString &window, QString *start, QString *end, QString *error) {
        const QString trimmed = window.trimmed();
        if (trimmed.isEmpty()) {
            start->clear();
            end->clear();
            return true;
        }
        const qsizetype dash = trimmed.indexOf(u'-');
        if (dash < 0) {
            *error = u"the window must be HH:MM-HH:MM, or empty for any time"_s;
            return false;
        }
        const QString from = trimmed.left(dash).trimmed();
        const QString to = trimmed.mid(dash + 1).trimmed();
        for (const QString &clock: {from, to}) {
            if (!parseClock(clock)) {
                *error = u"invalid time \"%1\", expected HH:MM"_s.arg(clock);
                return false;
            }
        }
        *start = from;
        *end = to;
        return true;
    }

    QString settingsPath() {
        if (const QString path = qEnvironmentVariable("ULTRAMARINE_UPDATES_CONFIG"); !path.isEmpty()) {
            return path;
        }
        return u"/etc/ultramarine-updates/settings.json"_s;
    }

    bool parseSettings(const QByteArray &json, Settings *settings, QString *error) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            *error =
                parseError.error != QJsonParseError::NoError ? parseError.errorString() : u"expected a JSON object"_s;
            return false;
        }
        const QJsonObject object = document.object();
        Settings parsed;
        const auto readBool = [&object, error](const QString &key, bool *value) {
            const QJsonValue json_value = object.value(key);
            if (json_value.isUndefined()) {
                return true;
            }
            if (!json_value.isBool()) {
                *error = u"%1 must be true or false"_s.arg(key);
                return false;
            }
            *value = json_value.toBool();
            return true;
        };
        const auto readString = [&object, error](const QString &key, QString *value) {
            const QJsonValue json_value = object.value(key);
            if (json_value.isUndefined()) {
                return true;
            }
            if (!json_value.isString()) {
                *error = u"%1 must be a string"_s.arg(key);
                return false;
            }
            *value = json_value.toString();
            return true;
        };
        if (!readBool(u"automatic"_s, &parsed.automatic) || !readBool(u"offline"_s, &parsed.offline) ||
            !readString(u"window_start"_s, &parsed.windowStart) || !readString(u"window_end"_s, &parsed.windowEnd)) {
            return false;
        }
        if (const QJsonValue minutes = object.value("inactive_minutes"_L1); !minutes.isUndefined()) {
            const double value = minutes.toDouble(-1);
            if (!minutes.isDouble() || std::trunc(value) != value) {
                *error = u"inactive_minutes must be a whole number"_s;
                return false;
            }
            parsed.inactiveMinutes = static_cast<int>(value);
        }
        if (const QString invalid = parsed.validate(); !invalid.isEmpty()) {
            *error = invalid;
            return false;
        }
        *settings = std::move(parsed);
        return true;
    }

    bool loadSettings(Settings *settings, QString *error) {
        *settings = Settings{};
        QFile file(settingsPath());
        if (!file.exists()) {
            return true;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            *error = u"%1: %2"_s.arg(settingsPath(), file.errorString());
            return false;
        }
        QString parseError;
        if (!parseSettings(file.readAll(), settings, &parseError)) {
            *settings = Settings{};
            *error = u"invalid %1: %2"_s.arg(settingsPath(), parseError);
            return false;
        }
        return true;
    }

    bool saveSettings(const Settings &settings, QString *error) {
        const QString path = settingsPath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            *error = u"could not create %1"_s.arg(QFileInfo(path).absolutePath());
            return false;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = u"%1: %2"_s.arg(path, file.errorString());
            return false;
        }
        file.write(QJsonDocument(settings.toJson()).toJson(QJsonDocument::Indented));
        if (!file.commit()) {
            *error = u"%1: %2"_s.arg(path, file.errorString());
            return false;
        }
        // QSaveFile's temporary file is private; the KCM and user timers must read it.
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        return true;
    }

} // namespace UltramarineUpdates
