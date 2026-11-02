// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// ultramarine-updates: the backend of the Ultramarine Updates KCM.
//
// the KCM runs it as the logged-in user. Commands that need root run their
// system part as a child through pkexec (no terminal: a polkit prompt) or sudo
// (from a terminal), and only then the user's own part (user Flatpaks, Nix
// profile, Plasma widgets) as the user.

#include <KLocalizedString>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSet>
#include <QStandardPaths>

#include <cstdio>

#include <unistd.h>

#include "image.h"
#include "lock.h"
#include "packagekit.h"
#include "process.h"
#include "schedule.h"
#include "settings.h"
#include "updates.h"

using namespace Qt::StringLiterals;
using namespace UltramarineUpdates;

namespace {

    /// environment forwarded through sudo (pkexec always clears it)
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
     * runs `<this helper> @p arguments` as root and returns its exit code: pkexec
     * exits with 126 if the prompt is dismissed and 127 if authorization fails,
     * which the KCM checks for. The polkit actions match our exact path and the
     * command (argv[1]), so @p arguments must start with the command. pkexec runs
     * it with a clean environment, so wrapping the helper in `env` would stop the
     * actions from applying.
     */
    int runAsRoot(const QStringList &arguments) {
        const QString self = QCoreApplication::applicationFilePath();
        QString program;
        QStringList argv;
        if (const QString pkexec = QStandardPaths::findExecutable(u"pkexec"_s);
            !isatty(STDIN_FILENO) && !pkexec.isEmpty()) {
            program = pkexec;
        } else {
            program = u"/usr/bin/sudo"_s;
            for (const QString &name: forwardedEnvironment) {
                if (qEnvironmentVariableIsSet(name.toLatin1().constData())) {
                    argv << name + u'=' + qEnvironmentVariable(name.toLatin1().constData());
                }
            }
        }
        argv << self << arguments;
        std::fflush(stdout);
        std::fflush(stderr);
        const int exitCode = runForwarded(program, argv);
        if (exitCode < 0) {
            std::fprintf(stderr, "could not run %s\n", qPrintable(program));
            return 1;
        }
        return exitCode;
    }

    /// this command line, rebuilt for runAsRoot() with the command first
    QStringList commandLine(const QString &command, const QCommandLineParser &parser) {
        static const QSet<QString> valueOptions{u"automatic"_s, u"offline"_s, u"inactive-minutes"_s, u"window"_s};
        QStringList arguments{command};
        for (const QStringList names = parser.optionNames(); const QString &name: names) {
            if (valueOptions.contains(name)) {
                arguments << u"--%1=%2"_s.arg(name, parser.value(name));
            } else {
                arguments << (name == "y"_L1 ? u"--yes"_s : u"--"_s + name);
            }
        }
        return arguments;
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
            if (!response.offlineError.isEmpty()) {
                std::fprintf(stderr, "warning: the updates installed while restarting failed: %s\n",
                             qPrintable(response.offlineError));
            }
        }
        // only fail when nothing could be queried at all
        return !response.errors.isEmpty() && response.updates.isEmpty() ? 1 : 0;
    }

    int applyCommand(const QString &command, const QCommandLineParser &parser) {
        const bool assumeYes = parser.isSet(u"yes"_s);
        QString error;
        if (geteuid() != 0) {
            InstallLock userLock(userLockPath());
            if (const InstallLock::Result locked = userLock.tryLock(&error); locked != InstallLock::Result::Acquired) {
                return fail(locked == InstallLock::Result::Busy ? installBusyMessage() : error);
            }
            // system updates first: if authentication is canceled (or they fail),
            // nothing is installed, and pkexec's exit codes are passed on
            if (const int exitCode = runAsRoot(commandLine(command, parser)); exitCode != 0) {
                return exitCode;
            }
            // user-level updates must run as the invoking user
            return applyUser(assumeYes, &error) ? 0 : fail(error);
        }

        InstallLock systemLock(systemLockPath());
        if (const InstallLock::Result locked = systemLock.tryLock(&error); locked != InstallLock::Result::Acquired) {
            return fail(locked == InstallLock::Result::Busy ? installBusyMessage() : error);
        }
        // read as root: pkexec cleared any development override
        Settings settings;
        if (!loadSettings(&settings, &error)) {
            std::fprintf(stderr, "warning: %s\n", qPrintable(error));
        }
        bool updated = false;
        if (!applySystem(assumeYes, settings.offline, &updated, &error)) {
            return fail(error);
        }
        if (!updated) {
            return fail(u"no supported package manager found"_s);
        }
        if (!isImageMode() && offlineUpdatePending()) {
            print(u"System updates will be installed during the next restart.\n"_s);
        }
        return 0;
    }

    int settingsCommand(const QString &command, const QCommandLineParser &parser) {
        const bool changing = parser.isSet(u"automatic"_s) || parser.isSet(u"offline"_s) ||
            parser.isSet(u"inactive-minutes"_s) || parser.isSet(u"window"_s);
        QString error;
        if (changing) {
            // ULTRAMARINE_UPDATES_CONFIG points at a development file, and escalating
            // would drop it (pkexec clears the environment) and change the real settings
            if (geteuid() != 0 && qEnvironmentVariableIsEmpty("ULTRAMARINE_UPDATES_CONFIG")) {
                return runAsRoot(commandLine(command, parser));
            }
            Settings settings;
            // broken settings are replaced instead of getting stuck on them
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

    int scheduleCommand(const QString &command, const QCommandLineParser &parser) {
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
        if (geteuid() != 0) {
            // the system's timer first: if authentication is canceled, nothing
            // changes. Then the user's own updates, in their user manager.
            if (const int exitCode = runAsRoot(commandLine(command, parser)); exitCode != 0) {
                return exitCode;
            }
            if (cancel) {
                if (!cancelTonight(true, &error)) {
                    return fail(u"failed to cancel your updates: %1"_s.arg(error));
                }
            } else if (!scheduleTonight(true, &error)) {
                // don't leave only the system's half scheduled
                runAsRoot({command, u"--cancel"_s});
                return fail(u"failed to schedule your updates: %1"_s.arg(error));
            }
            return 0;
        }
        if (cancel) {
            if (!cancelTonight(false, &error)) {
                return fail(u"failed to cancel updates: %1"_s.arg(error));
            }
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
    parser.setApplicationDescription(
        u"Lists and installs RPM (or bootc image), Flatpak, Nix and Plasma widget updates.\n\n"
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
        return applyCommand(command, parser);
    }
    if (command == "settings"_L1) {
        return settingsCommand(command, parser);
    }
    if (command == "schedule"_L1) {
        return scheduleCommand(command, parser);
    }
    QString error;
    const bool tonight = parser.isSet(u"tonight"_s);
    const bool ok = command == "run-scheduled"_L1 ? runScheduled(tonight, &error) : runUser(tonight, &error);
    return ok ? 0 : fail(error);
}
