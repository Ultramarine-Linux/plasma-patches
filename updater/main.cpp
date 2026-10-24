// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// ultramarine-updates: the backend of the Ultramarine Updates KCM.
//
// The KCM runs it as the logged-in user. Commands that need root re-execute
// through pkexec (no terminal: a polkit prompt) or sudo (from a terminal), after
// doing the user's own part (user Flatpaks, Nix profile) as the user.

#include <KLocalizedString>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>
#include <vector>

#include <unistd.h>

#include "schedule.h"
#include "settings.h"
#include "updates.h"

using namespace Qt::StringLiterals;
using namespace UltramarineUpdates;

namespace {

    /// Environment forwarded through sudo (pkexec always clears it).
    const QStringList forwardedEnvironment{u"ULTRAMARINE_UPDATES_CONFIG"_s, u"ULTRAMARINE_UPDATES_STATE"_s};

    void print(const QString &text) {
        std::fputs(qPrintable(text), stdout);
        std::fflush(stdout);
    }

    void printJson(const QJsonObject &object) {
        print(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) + u'\n');
    }

    int fail(const QString &message) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
        return 1;
    }

    /**
     * Re-executes this command as root if it isn't already, and only returns as
     * root (or with an error message). pkexec exits with 126 if the prompt is
     * dismissed and 127 if authorization fails; the KCM checks for both. pkexec
     * runs us with a clean environment: wrapping the helper in `env` would stop
     * the polkit action, which matches our exact path, from applying.
     */
    QString escalate() {
        if (geteuid() == 0) {
            return {};
        }
        const QString self = QCoreApplication::applicationFilePath();
        const QStringList arguments = QCoreApplication::arguments().mid(1);

        QString program;
        QStringList argv;
        if (const QString pkexec = QStandardPaths::findExecutable(u"pkexec"_s);
            !isatty(STDIN_FILENO) && !pkexec.isEmpty()) {
            program = pkexec;
            argv << u"pkexec"_s << self << arguments;
        } else {
            program = u"/usr/bin/sudo"_s;
            argv << u"sudo"_s;
            for (const QString &name: forwardedEnvironment) {
                if (qEnvironmentVariableIsSet(name.toLatin1().constData())) {
                    argv << name + u'=' + qEnvironmentVariable(name.toLatin1().constData());
                }
            }
            argv << self << arguments;
        }

        std::vector<QByteArray> storage(static_cast<size_t>(argv.size()));
        std::ranges::transform(argv, storage.begin(), [](const QString &argument) { return argument.toLocal8Bit(); });
        std::vector<char *> raw(storage.size() + 1, nullptr);
        std::ranges::transform(storage, raw.begin(), [](QByteArray &argument) { return argument.data(); });

        std::fflush(stdout);
        std::fflush(stderr);
        execv(program.toLocal8Bit().constData(), raw.data());
        return u"could not run %1"_s.arg(program);
    }

    std::optional<bool> parseBool(const QString &value) {
        const QString lower = value.toLower();
        if (lower == "true"_L1 || lower == "yes"_L1 || lower == "1"_L1) {
            return true;
        }
        if (lower == "false"_L1 || lower == "no"_L1 || lower == "0"_L1) {
            return false;
        }
        return std::nullopt;
    }

    int listCommand(const QCommandLineParser &parser) {
        const ListResponse response = listUpdates();
        if (parser.isSet(u"json"_s)) {
            printJson(response.toJson());
        } else {
            for (const PackageUpdate &update: response.updates) {
                print(u"%1: %2 -> %3 (%4)\n"_s.arg(update.name, update.oldVersion, update.newVersion, update.source));
            }
            for (const QString &error: response.errors) {
                std::fprintf(stderr, "warning: failed to list updates from %s\n", qPrintable(error));
            }
        }
        // Only fail when nothing could be queried at all.
        return !response.errors.isEmpty() && response.updates.isEmpty() ? 1 : 0;
    }

    int applyCommand(const QCommandLineParser &parser) {
        const bool assumeYes = parser.isSet(u"yes"_s);
        QString error;
        // User-level updates must run as the invoking user, before escalating.
        if (geteuid() != 0 && !applyUser(assumeYes, &error)) {
            return fail(error);
        }
        if (const QString escalateError = escalate(); !escalateError.isEmpty()) {
            return fail(escalateError);
        }
        bool updated = false;
        if (!applySystem(assumeYes, false, &updated, &error)) {
            return fail(error);
        }
        return updated ? 0 : fail(u"no supported package manager found"_s);
    }

    int settingsCommand(const QCommandLineParser &parser) {
        const bool changing = parser.isSet(u"automatic"_s) || parser.isSet(u"offline"_s) ||
            parser.isSet(u"inactive-minutes"_s) || parser.isSet(u"window"_s);
        QString error;
        if (changing) {
            // ULTRAMARINE_UPDATES_CONFIG points at a development file; escalating
            // would drop it (pkexec clears the environment) and change the real settings.
            if (qEnvironmentVariableIsEmpty("ULTRAMARINE_UPDATES_CONFIG")) {
                if (const QString escalateError = escalate(); !escalateError.isEmpty()) {
                    return fail(escalateError);
                }
            }
            Settings settings;
            // Broken settings are replaced instead of getting stuck on them.
            loadSettings(&settings, &error);
            const auto readBool = [&parser](const QString &name, bool *value) {
                if (!parser.isSet(name)) {
                    return true;
                }
                const std::optional<bool> parsed = parseBool(parser.value(name));
                if (parsed) {
                    *value = *parsed;
                }
                return parsed.has_value();
            };
            if (!readBool(u"automatic"_s, &settings.automatic)) {
                return fail(u"--automatic must be true or false"_s);
            }
            if (!readBool(u"offline"_s, &settings.offline)) {
                return fail(u"--offline must be true or false"_s);
            }
            if (parser.isSet(u"inactive-minutes"_s)) {
                bool ok = false;
                settings.inactiveMinutes = parser.value(u"inactive-minutes"_s).toInt(&ok);
                if (!ok) {
                    return fail(u"--inactive-minutes must be a number"_s);
                }
            }
            if (parser.isSet(u"window"_s) &&
                !parseWindow(parser.value(u"window"_s), &settings.windowStart, &settings.windowEnd, &error)) {
                return fail(error);
            }
            if (const QString invalid = settings.validate(); !invalid.isEmpty()) {
                return fail(invalid);
            }
            if (!saveSettings(settings, &error)) {
                return fail(u"failed to save settings: %1"_s.arg(error));
            }
        }

        Settings settings;
        if (!loadSettings(&settings, &error)) {
            return fail(error);
        }
        if (parser.isSet(u"json"_s)) {
            printJson(settings.toJson());
            return 0;
        }
        const QString window =
            settings.windowStart.isEmpty() ? u"any time"_s : settings.windowStart + u'-' + settings.windowEnd;
        const auto yesNo = [](const bool value) { return value ? u"true"_s : u"false"_s; };
        print(u"Automatic updates: %1\n"
              u"Install when the device hasn't been used for: %2 minutes\n"
              u"Only install during: %3\n"
              u"Install system updates while restarting: %4\n"_s.arg(yesNo(settings.automatic))
                  .arg(settings.inactiveMinutes)
                  .arg(window, yesNo(settings.offline)));
        return 0;
    }

    int scheduleCommand(const QCommandLineParser &parser) {
        if (parser.isSet(u"status"_s)) {
            const ScheduleStatus status = tonightStatus();
            if (parser.isSet(u"json"_s)) {
                printJson(status.toJson());
            } else {
                print(status.scheduled ? u"Updates are scheduled to install tonight.\n"_s
                                       : u"No updates are scheduled.\n"_s);
            }
            return 0;
        }

        const bool cancel = parser.isSet(u"cancel"_s);
        QString error;
        // The user's own updates are scheduled in their user manager first;
        // escalating then re-runs this command as root for the system part.
        if (geteuid() != 0) {
            if (cancel) {
                cancelTonight(true);
            } else if (!scheduleTonight(true, &error)) {
                return fail(u"failed to schedule your updates: %1"_s.arg(error));
            }
        }
        if (const QString escalateError = escalate(); !escalateError.isEmpty()) {
            return fail(escalateError);
        }
        if (cancel) {
            cancelTonight(false);
            print(u"Tonight's updates were cancelled.\n"_s);
            return 0;
        }
        if (!scheduleTonight(false, &error)) {
            return fail(u"failed to schedule updates: %1"_s.arg(error));
        }
        print(u"Updates will be installed tonight, when the device isn't in use.\n"_s);
        return 0;
    }

} // namespace

int main(int argc, char *argv[]) {
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"ultramarine-updates"_s);
    KLocalizedString::setApplicationDomain("kcm_ultramarine_updates");

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Lists and installs RPM (or bootc image), Flatpak and Nix updates.\n\n"
                                     u"Commands:\n"
                                     u"  list            List available updates (--json)\n"
                                     u"  apply           Install available updates (--yes)\n"
                                     u"  settings        Show or change automatic update settings (--json, "
                                     u"--automatic, --offline, --inactive-minutes, --window)\n"
                                     u"  schedule        Install updates tonight, when the device isn't in use "
                                     u"(--cancel, --status, --json)\n"
                                     u"  run-scheduled   Install system updates if the device isn't in use (system "
                                     u"timers; --tonight)\n"
                                     u"  run-user        Install your updates or notify about them (user timers; "
                                     u"--tonight)"_s);
    parser.addHelpOption();
    parser.addPositionalArgument(u"command"_s, u"The command to run."_s);
    parser.addOptions({
        {u"json"_s, u"Output JSON."_s},
        {{u"y"_s, u"yes"_s}, u"Don't ask for confirmation."_s},
        {u"automatic"_s, u"Install updates automatically when the device isn't in use (false: only notify)."_s,
         u"true|false"_s},
        {u"offline"_s, u"Install system updates while restarting instead of on the running system."_s, u"true|false"_s},
        {u"inactive-minutes"_s, u"Install once no one has used the device for this many minutes."_s, u"minutes"_s},
        {u"window"_s, u"Only install between these times, e.g. 01:00-05:00 (empty: any time)."_s, u"HH:MM-HH:MM"_s},
        {u"cancel"_s, u"Cancel tonight's updates."_s},
        {u"status"_s, u"Show whether updates are scheduled."_s},
        {u"tonight"_s, u"Run for Apply Tonight, even if automatic updates are off."_s},
    });
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        std::fputs(qPrintable(parser.helpText()), stderr);
        return 1;
    }
    const QString &command = positional.constFirst();

    static const QHash<QString, QSet<QString>> allowedOptions{
        {u"list"_s, {u"json"_s}},
        {u"apply"_s, {u"y"_s, u"yes"_s}},
        {u"settings"_s, {u"json"_s, u"automatic"_s, u"offline"_s, u"inactive-minutes"_s, u"window"_s}},
        {u"schedule"_s, {u"cancel"_s, u"status"_s, u"json"_s}},
        {u"run-scheduled"_s, {u"tonight"_s}},
        {u"run-user"_s, {u"tonight"_s}},
    };
    const auto allowed = allowedOptions.constFind(command);
    if (allowed == allowedOptions.constEnd()) {
        return fail(u"Unknown command \"%1\". See --help."_s.arg(command));
    }
    for (const QStringList names = parser.optionNames(); const QString &name: names) {
        if (!allowed->contains(name)) {
            return fail(u"--%1 can't be used with %2."_s.arg(name, command));
        }
    }

    if (command == "list"_L1) {
        return listCommand(parser);
    }
    if (command == "apply"_L1) {
        return applyCommand(parser);
    }
    if (command == "settings"_L1) {
        return settingsCommand(parser);
    }
    if (command == "schedule"_L1) {
        return scheduleCommand(parser);
    }
    QString error;
    const bool tonight = parser.isSet(u"tonight"_s);
    const bool ok = command == "run-scheduled"_L1 ? runScheduled(tonight, &error) : runUser(tonight, &error);
    return ok ? 0 : fail(error);
}
