// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

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

    constexpr int pkexecDismissed = 126;
    constexpr int pkexecNotAuthorized = 127;
    constexpr int sudoAuthenticationFailed = 1;
    constexpr int systemApplyFailed = 2;
    constexpr std::chrono::minutes imageCheckWait{15};

    const QStringList environmentForwardedThroughSudo{u"ULTRAMARINE_UPDATES_CONFIG"_s, u"ULTRAMARINE_UPDATES_STATE"_s};

    void print(const QString &text) {
        std::fputs(qPrintable(text), stdout);
        std::fflush(stdout);
    }

    void printLine(const QString &text) { print(text + u'\n'); }

    void printJson(const QJsonObject &object) {
        printLine(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    }

    void printError(const QString &message) { std::fprintf(stderr, "%s\n", qPrintable(message)); }

    int fail(const QString &message, const int exitCode = 1) {
        printError(message);
        return exitCode;
    }

    void warn(const QString &message) {
        printError(i18nc("@info:status %1 is a warning message", "Warning: %1", message));
    }

    int runAsRoot(const QStringList &escalatedArguments, bool *ranThroughSudo = nullptr) {
        const QString self = QCoreApplication::applicationFilePath();
        QString program;
        QStringList argv;
        if (const QString pkexec = QStandardPaths::findExecutable(u"pkexec"_s);
            !isatty(STDIN_FILENO) && !pkexec.isEmpty()) {
            program = pkexec;
        } else {
            program = u"/usr/bin/sudo"_s;
            for (const QString &name: environmentForwardedThroughSudo) {
                if (const QByteArray variable = name.toLatin1(); qEnvironmentVariableIsSet(variable.constData())) {
                    argv << name + u'=' + qEnvironmentVariable(variable.constData());
                }
            }
        }
        if (ranThroughSudo) {
            *ranThroughSudo = !program.endsWith("/pkexec"_L1);
        }
        argv << self << escalatedArguments;
        std::fflush(stdout);
        std::fflush(stderr);
        const int exitCode = runForwarded(program, argv);
        if (exitCode < 0) {
            return fail(i18nc("@info:status %1 is a program", "Could not run %1", program));
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
                printError(i18nc("@info:status %1 is a source and its error message",
                                 "Warning: failed to list updates from %1", error));
            }
            if (!response.offlineError.isEmpty()) {
                printError(i18nc("@info:status %1 is an error message",
                                 "Warning: the updates installed while restarting failed: %1", response.offlineError));
            }
        }
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
            bool ranThroughSudo = false;
            int systemExitCode = runAsRoot(escalatedCommandLine(command, parser), &ranThroughSudo);
            if (ranThroughSudo && systemExitCode == sudoAuthenticationFailed) {
                systemExitCode = pkexecNotAuthorized;
            }
            if (systemExitCode == pkexecDismissed || systemExitCode == pkexecNotAuthorized) {
                return fail(i18nc("@info:status", "Authentication was canceled or failed. Nothing was installed."),
                            systemExitCode);
            }
            const ShutdownInhibitor inhibitor(i18nc("@info the reason shutting down waits", "Installing your updates"));
            const ApplyResult result = applyUser(assumeYes);
            if (result.updated && result.countsAsInstalled()) {
                recordManualInstall(true, QDateTime::currentDateTime());
            }
            if (!result.ok()) {
                fail(result.error());
                return systemExitCode != 0 ? systemExitCode : 1;
            }
            return systemExitCode;
        }

        const auto failSystem = [](const QString &message) { return fail(message, systemApplyFailed); };
        constexpr std::chrono::seconds briefLockHolderWait{2};
        InstallLock systemLock(systemLockPath());
        InstallLock::Result locked = systemLock.tryLock(&error, briefLockHolderWait);
        const auto imageCheckRunning = [] {
            return InstallLock(imageCheckLockPath()).tryLock() == InstallLock::Result::Busy;
        };
        if (locked == InstallLock::Result::Busy && imageCheckRunning()) {
            printLine(i18nc("@info:progress", "Waiting for the check for a new system image to finish…"));
            locked = systemLock.tryLock(&error, imageCheckWait);
        }
        if (locked != InstallLock::Result::Acquired) {
            return failSystem(locked == InstallLock::Result::Busy ? installBusyMessage() : error);
        }
        QString settingsWarning;
        const Settings settings = loadSettingsOrDefaults(&settingsWarning);
        if (!settingsWarning.isEmpty()) {
            warn(settingsWarning);
        }
        const ShutdownInhibitor inhibitor(i18nc("@info the reason shutting down waits", "Installing system updates"));
        const ApplyResult result = applySystem(assumeYes, settings.offline);
        if (result.updated && result.countsAsInstalled()) {
            recordManualInstall(false, QDateTime::currentDateTime());
        }
        if (!result.ok()) {
            return failSystem(result.error());
        }
        if (!result.updated) {
            return failSystem(i18nc("@info:status", "No supported package manager was found."));
        }
        if (!isImageMode() && offlineUpdatePending()) {
            printLine(i18nc("@info:status", "System updates will be installed during the next restart."));
        }
        return 0;
    }

    QString changeSettings(const QCommandLineParser &parser) {
        Settings settings;
        QString error;
        loadSettings(&settings, &error);
        const auto readBool = [&parser](const QString &name, bool *value) {
            if (!parser.isSet(name)) {
                return true;
            }
            const std::optional<bool> parsed = parseBool(parser.value(name));
            *value = parsed.value_or(*value);
            return parsed.has_value();
        };
        if (!readBool(u"automatic"_s, &settings.automatic)) {
            return i18nc("@info:shell", "--automatic must be true or false.");
        }
        if (!readBool(u"offline"_s, &settings.offline)) {
            return i18nc("@info:shell", "--offline must be true or false.");
        }
        if (parser.isSet(u"inactive-minutes"_s)) {
            bool ok = false;
            settings.inactiveMinutes = parser.value(u"inactive-minutes"_s).toInt(&ok);
            if (!ok) {
                return i18nc("@info:shell", "--inactive-minutes must be a number.");
            }
        }
        if (parser.isSet(u"window"_s) &&
            !parseWindow(parser.value(u"window"_s), &settings.windowStart, &settings.windowEnd, &error)) {
            return error;
        }
        if (const QString invalid = settings.validate(); !invalid.isEmpty()) {
            return invalid;
        }
        if (!saveSettings(settings, &error)) {
            return i18nc("@info:status %1 is an error message", "Failed to save the settings: %1", error);
        }
        return {};
    }

    int settingsCommand(const QString &command, const QCommandLineParser &parser) {
        const bool changing = parser.isSet(u"automatic"_s) || parser.isSet(u"offline"_s) ||
            parser.isSet(u"inactive-minutes"_s) || parser.isSet(u"window"_s);
        if (changing) {
            if (const bool usingDevelopmentSettings = !qEnvironmentVariableIsEmpty("ULTRAMARINE_UPDATES_CONFIG");
                geteuid() != 0 && !usingDevelopmentSettings) {
                return runAsRoot(escalatedCommandLine(command, parser));
            }
            if (const QString error = changeSettings(parser); !error.isEmpty()) {
                return fail(error);
            }
        }

        QString error;
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

    void printTonightStatus(const ScheduleStatus &status, const bool json) {
        if (json) {
            printJson(status.toJson());
        } else if (status.scheduled) {
            printLine(i18nc("@info:status", "Updates are scheduled to install tonight."));
        } else if (status.partial) {
            printLine(i18nc("@info:status",
                            "Only some of tonight's updates are still scheduled. Schedule them "
                            "again to install all of them."));
        } else {
            printLine(i18nc("@info:status", "No updates are scheduled."));
        }
    }

    int scheduleCommand(const QString &command, const QCommandLineParser &parser) {
        if (parser.isSet(u"status"_s)) {
            printTonightStatus(tonightStatus(), parser.isSet(u"json"_s));
            return 0;
        }

        const bool cancel = parser.isSet(u"cancel"_s);
        QString error;
        if (geteuid() != 0) {
            if (const int exitCode = runAsRoot(escalatedCommandLine(command, parser)); exitCode != 0) {
                return exitCode;
            }
            if (cancel) {
                if (!cancelTonight(true, &error)) {
                    return fail(
                        i18nc("@info:status %1 is an error message", "Failed to cancel your updates: %1", error));
                }
            } else if (!scheduleTonight(true, &error)) {
                runAsRoot({command, u"--cancel"_s});
                return fail(i18nc("@info:status %1 is an error message", "Failed to schedule your updates: %1", error));
            }
            return 0;
        }
        if (cancel) {
            if (!cancelTonight(false, &error)) {
                return fail(i18nc("@info:status %1 is an error message", "Failed to cancel the updates: %1", error));
            }
            printLine(i18nc("@info:status", "Tonight's updates were canceled."));
            return 0;
        }
        if (!scheduleTonight(false, &error)) {
            return fail(i18nc("@info:status %1 is an error message", "Failed to schedule the updates: %1", error));
        }
        printLine(i18nc("@info:status", "Updates will be installed tonight, when the device isn't in use."));
        return 0;
    }

    void keepInstallingWhenTheKcmCloses() { std::signal(SIGPIPE, SIG_IGN); }

} // namespace

int main(int argc, char *argv[]) {
    keepInstallingWhenTheKcmCloses();
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"ultramarine-updates"_s);

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
