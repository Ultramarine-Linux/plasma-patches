// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "settings.h"

#include <KLocalizedString>

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

        void makeDirectoryWorldReadable(const QString &directory) {
            QFile::setPermissions(directory,
                                  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                                      QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
        }

        void makeFileWorldReadable(const QString &path) {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        }
    } // namespace

    QString Settings::validate() const {
        if (inactiveMinutes < 0 || inactiveMinutes > maxInactiveMinutes) {
            return i18nc("@info:status", "The inactive time must be between 0 and %1 minutes.", maxInactiveMinutes);
        }
        if (windowStart.isEmpty() != windowEnd.isEmpty()) {
            return i18nc("@info:status", "A time window needs both a start and an end.");
        }
        for (const QString &clock: {windowStart, windowEnd}) {
            if (!clock.isEmpty() && !parseClock(clock)) {
                return i18nc("@info:status %1 is what was entered. Don't translate HH:MM.",
                             "Invalid time \"%1\", expected HH:MM.", clock);
            }
        }
        if (!windowStart.isEmpty() && parseClock(windowStart) == parseClock(windowEnd)) {
            return i18nc("@info:status", "A time window's start and end must differ.");
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
            *error = i18nc("@info:status Don't translate HH:MM-HH:MM.",
                           "The window must be HH:MM-HH:MM, or empty for any time.");
            return false;
        }
        const QString from = trimmed.left(dash).trimmed();
        const QString to = trimmed.mid(dash + 1).trimmed();
        for (const QString &clock: {from, to}) {
            if (!parseClock(clock)) {
                *error = i18nc("@info:status %1 is what was entered. Don't translate HH:MM.",
                               "Invalid time \"%1\", expected HH:MM.", clock);
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
            *error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                  : i18nc("@info:status", "expected a JSON object");
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
                *error =
                    i18nc("@info:status %1 is a settings key, don't translate it", "%1 must be true or false", key);
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
                *error = i18nc("@info:status %1 is a settings key, don't translate it", "%1 must be a string", key);
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
                *error =
                    i18nc("@info:status don't translate inactive_minutes", "inactive_minutes must be a whole number");
                return false;
            }
            if (value < 0 || value > maxInactiveMinutes) {
                *error =
                    i18nc("@info:status", "The inactive time must be between 0 and %1 minutes.", maxInactiveMinutes);
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
            *error = i18nc("@info:status %1 is a file path, %2 an error message", "Invalid %1: %2", settingsPath(),
                           parseError);
            return false;
        }
        return true;
    }

    Settings loadSettingsOrDefaults(QString *warning) {
        warning->clear();
        Settings settings;
        loadSettings(&settings, warning);
        return settings;
    }

    bool saveSettings(const Settings &settings, QString *error) {
        const QString path = settingsPath();
        const QString directory = QFileInfo(path).absolutePath();
        const bool created = !QFileInfo::exists(directory);
        if (!QDir().mkpath(directory)) {
            *error = i18nc("@info:status %1 is a directory", "Could not create %1", directory);
            return false;
        }
        if (created) {
            makeDirectoryWorldReadable(directory);
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
        makeFileWorldReadable(path);
        return true;
    }

} // namespace UltramarineUpdates
