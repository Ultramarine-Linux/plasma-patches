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
#include <QStandardPaths>

#include <csignal>
#include <cstdio>

#include <unistd.h>

#include "cli.h"
#include "image.h"
#include "inhibit.h"
#include "lock.h"
#include "packagekit.h"
#include "process.h"
#include "schedule.h"
#include "settings.h"
#include "updates.h"

using namespace Qt::StringLiterals;
using namespace UltramarineUpdates;

namespace {

    /// pkexec's exit codes when the prompt is dismissed and when authorization fails
    constexpr int pkexecDismissed = 126;
    constexpr int pkexecNotAuthorized = 127;

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
            std::fprintf(stderr, "%s\n",
                         qPrintable(i18nc("@info:status %1 is a program", "Could not run %1", program)));
            return 1;
        }
        return exitCode;
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
                std::fprintf(stderr, "%s\n",
                             qPrintable(i18nc("@info:status %1 is a source and its error message",
                                              "Warning: failed to list updates from %1", error)));
            }
            if (!response.offlineError.isEmpty()) {
                std::fprintf(stderr, "%s\n",
                             qPrintable(i18nc("@info:status %1 is an error message",
                                              "Warning: the updates installed while restarting failed: %1",
                                              response.offlineError)));
            }
        }
        // only fail when nothing could be queried at all
        return response.allFailed() ? 1 : 0;
    }

    int applyCommand(const QString &command, const QCommandLineParser &parser) {
        const bool assumeYes = parser.isSet(u"yes"_s);
        QString error;
        if (geteuid() != 0) {
            InstallLock userLock(userLockPath());
            if (const InstallLock::Result locked = userLock.tryLock(&error); locked != InstallLock::Result::Acquired) {
                return fail(locked == InstallLock::Result::Busy ? installBusyMessage() : error);
            }
            // system updates first: if authentication is canceled, nothing is
            // installed, and pkexec's exit codes are passed on
            const int systemExitCode = runAsRoot(escalatedCommandLine(command, parser));
            if (systemExitCode == pkexecDismissed || systemExitCode == pkexecNotAuthorized) {
                return systemExitCode;
            }
            // user-level updates must run as the invoking user. They're installed
            // even if some system updates failed, which the root part already reported.
            const ShutdownInhibitor inhibitor(i18nc("@info the reason shutting down waits", "Installing your updates"));
            if (const ApplyResult result = applyUser(assumeYes); !result.ok()) {
                fail(result.error());
                return systemExitCode != 0 ? systemExitCode : 1;
            }
            return systemExitCode;
        }

        // the system timer holds this lock for a moment to see whether an install
        // runs before checking for a new image, so wait that long for it
        constexpr std::chrono::seconds probeWait{2};
        InstallLock systemLock(systemLockPath());
        if (const InstallLock::Result locked = systemLock.tryLock(&error, probeWait);
            locked != InstallLock::Result::Acquired) {
            return fail(locked == InstallLock::Result::Busy ? installBusyMessage() : error);
        }
        // read as root: pkexec cleared any development override, sudo forwards it
        Settings settings;
        if (!loadSettings(&settings, &error)) {
            std::fprintf(stderr, "%s\n",
                         qPrintable(i18nc("@info:status %1 is a warning message", "Warning: %1", error)));
        }
        const ShutdownInhibitor inhibitor(i18nc("@info the reason shutting down waits", "Installing system updates"));
        const ApplyResult result = applySystem(assumeYes, settings.offline);
        if (!result.ok()) {
            return fail(result.error());
        }
        if (!result.updated) {
            return fail(i18nc("@info:status", "No supported package manager was found."));
        }
        if (!isImageMode() && offlineUpdatePending()) {
            print(i18nc("@info:status", "System updates will be installed during the next restart.") + u'\n');
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
                return runAsRoot(escalatedCommandLine(command, parser));
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
                return fail(i18nc("@info:shell", "--automatic must be true or false."));
            }
            if (!readBool(u"offline"_s, &settings.offline)) {
                return fail(i18nc("@info:shell", "--offline must be true or false."));
            }
            if (parser.isSet(u"inactive-minutes"_s)) {
                bool ok = false;
                settings.inactiveMinutes = parser.value(u"inactive-minutes"_s).toInt(&ok);
                if (!ok) {
                    return fail(i18nc("@info:shell", "--inactive-minutes must be a number."));
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
                return fail(i18nc("@info:status %1 is an error message", "Failed to save the settings: %1", error));
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
                if (status.scheduled) {
                    print(i18nc("@info:status", "Updates are scheduled to install tonight.") + u'\n');
                } else if (status.partial) {
                    // usually the user's timer, which logging out stops
                    print(i18nc("@info:status",
                                "Only some of tonight's updates are still scheduled. Schedule them "
                                "again to install all of them.") +
                          u'\n');
                } else {
                    print(i18nc("@info:status", "No updates are scheduled.") + u'\n');
                }
            }
            return 0;
        }

        const bool cancel = parser.isSet(u"cancel"_s);
        QString error;
        if (geteuid() != 0) {
            // the system's timer first: if authentication is canceled, nothing
            // changes. Then the user's own updates, in their user manager.
            if (const int exitCode = runAsRoot(escalatedCommandLine(command, parser)); exitCode != 0) {
                return exitCode;
            }
            if (cancel) {
                if (!cancelTonight(true, &error)) {
                    return fail(
                        i18nc("@info:status %1 is an error message", "Failed to cancel your updates: %1", error));
                }
            } else if (!scheduleTonight(true, &error)) {
                // don't leave only the system's half scheduled
                runAsRoot({command, u"--cancel"_s});
                return fail(i18nc("@info:status %1 is an error message", "Failed to schedule your updates: %1", error));
            }
            return 0;
        }
        if (cancel) {
            if (!cancelTonight(false, &error)) {
                return fail(i18nc("@info:status %1 is an error message", "Failed to cancel the updates: %1", error));
            }
            print(i18nc("@info:status", "Tonight's updates were canceled.") + u'\n');
            return 0;
        }
        if (!scheduleTonight(false, &error)) {
            return fail(i18nc("@info:status %1 is an error message", "Failed to schedule the updates: %1", error));
        }
        print(i18nc("@info:status", "Updates will be installed tonight, when the device isn't in use.") + u'\n');
        return 0;
    }

} // namespace

int main(int argc, char *argv[]) {
    // if the KCM goes away mid-install, writing progress to its pipe fails with
    // EPIPE instead of killing the helper. Children keep this (see runForwarded()),
    // so dnf and flatpak finish too.
    std::signal(SIGPIPE, SIG_IGN);
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"ultramarine-updates"_s);
    // i18n() uses TRANSLATION_DOMAIN (ultramarine-plasma-settings), set at build time

    QCommandLineParser parser;
    setUpParser(parser);
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        std::fputs(qPrintable(parser.helpText()), stderr);
        return 1;
    }
    const QString &command = positional.constFirst();
    if (const QString invalid = checkOptions(command, parser); !invalid.isEmpty()) {
        return fail(invalid);
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
