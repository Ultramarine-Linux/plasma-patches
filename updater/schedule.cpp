// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "schedule.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>

#include <pwd.h>
#include <unistd.h>

#include "activity.h"
#include "image.h"
#include "inhibit.h"
#include "lock.h"
#include "packagekit.h"
#include "process.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace UltramarineUpdates {

    namespace {
        const auto tonightTimer = u"ultramarine-updates-tonight"_s;
        const auto tonightUserTimer = u"ultramarine-updates-tonight-user"_s;
        const auto tonightDefaultStart = u"22:00"_s;
        const auto tonightDefaultEnd = u"06:00"_s;
        const auto pollCalendar = u"*:0/5"_s;
        const auto tonightScheduledFile = u"/tonight-scheduled"_s;
        const QStringList developmentOverrides{u"ULTRAMARINE_UPDATES_CONFIG"_s, u"ULTRAMARINE_UPDATES_STATE"_s};
        const QStringList openUpdatesCommand{u"systemsettings"_s, u"kcm_ultramarine_updates"_s};

        constexpr std::chrono::seconds minTimeBetweenInstalls = 20h;
        constexpr std::chrono::seconds minTimeBetweenUpdateNotifications = 24h;
        constexpr std::chrono::seconds imageCheckInterval = 6h;
        constexpr std::chrono::seconds retryAfterFailedInstall = 1h;
        constexpr int systemctlUnitNotLoaded = 5;

        void warn(const QString &message) {
            std::fprintf(stderr, "%s\n",
                         qPrintable(i18nc("@info:status %1 is a warning message", "Warning: %1", message)));
        }

        void say(const QString &message) {
            std::puts(qPrintable(message));
            std::fflush(stdout);
        }

        void writeTimestampOrWarn(const QString &path, const QDateTime &time) {
            if (QString warning; !writeTimestamp(path, time, &warning)) {
                warn(warning);
            }
        }

        CommandResult systemctl(const bool userManager, QStringList arguments) {
            if (userManager) {
                arguments.prepend(u"--user"_s);
            }
            return capture(u"systemctl"_s, arguments);
        }

        QString stateDirFor(const bool userManager) { return userManager ? userStateDir() : systemStateDir(); }

        QString tonightTimerFor(const bool userManager) { return userManager ? tonightUserTimer : tonightTimer; }

        void stopTonightTimer(const bool userManager) {
            systemctl(userManager, {u"stop"_s, tonightTimerFor(userManager) + ".timer"_L1});
        }

        Settings loadSettingsOrWarn() {
            QString settingsWarning;
            Settings settings = loadSettingsOrDefaults(&settingsWarning);
            if (!settingsWarning.isEmpty()) {
                warn(settingsWarning);
            }
            return settings;
        }

        bool serviceRunning(const bool userManager, const QString &unit) {
            const QString state =
                QString::fromUtf8(
                    systemctl(userManager, {u"show"_s, u"-p"_s, u"ActiveState"_s, u"--value"_s, unit}).out)
                    .trimmed();
            return state == "activating"_L1 || state == "active"_L1 || state == "deactivating"_L1 ||
                state == "reloading"_L1;
        }

        QString tonightScheduledPath(const bool userManager) { return stateDirFor(userManager) + tonightScheduledFile; }

        bool replaceTransientTimer(const bool userManager, const QString &unit, const QStringList &arguments,
                                   QString *error) {
            if (serviceRunning(userManager, unit + ".service"_L1)) {
                say(i18nc("@info:status", "Tonight's updates are being installed right now."));
                return true;
            }
            systemctl(userManager, {u"stop"_s, unit + ".timer"_L1});
            systemctl(userManager, {u"reset-failed"_s, unit + ".service"_L1});

            QStringList runArguments;
            if (userManager) {
                runArguments.append(u"--user"_s);
            }
            runArguments << u"--unit="_s + unit << u"--on-calendar="_s + pollCalendar
                         << u"--timer-property=AccuracySec=1min"_s << u"--description=Install updates tonight"_s
                         << u"--property=Nice=19"_s << u"--property=IOSchedulingClass=idle"_s
                         << u"--property=TimeoutStartSec=6h"_s;
            if (!userManager) {
                runArguments << u"--property=Wants=network-online.target"_s
                             << u"--property=After=network-online.target"_s << u"--property=TimeoutStopSec=30min"_s;
            }
            for (const QString &name: developmentOverrides) {
                if (const QByteArray variable = name.toLatin1(); qEnvironmentVariableIsSet(variable.constData())) {
                    runArguments << u"--setenv=%1=%2"_s.arg(name, qEnvironmentVariable(variable.constData()));
                }
            }
            runArguments << u"--"_s << QCoreApplication::applicationFilePath();
            runArguments += arguments;
            writeTimestampOrWarn(tonightScheduledPath(userManager), QDateTime::currentDateTime());
            if (const CommandResult result = capture(u"systemd-run"_s, runArguments); !result.succeeded()) {
                *error = i18nc("@info:status %1 is an error message", "systemd-run failed: %1",
                               QString::fromUtf8(result.out + result.err).trimmed());
                return false;
            }
            return true;
        }

        QString currentUserName() {
            const passwd *entry = getpwuid(getuid()); // NOLINT(concurrency-mt-unsafe): single-threaded CLI
            return entry ? QString::fromLocal8Bit(entry->pw_name) : QString();
        }

        // notify-send blocks until the notification is closed, which can be never, so it gets its own service
        void showUpdatesNotification(const qsizetype count) {
            const QString readyMessage =
                i18ncp("@info", "1 update is ready to install.", "%1 updates are ready to install.", count);
            const QString notifySend = QStandardPaths::findExecutable(u"notify-send"_s);
            if (notifySend.isEmpty()) {
                say(readyMessage);
                return;
            }
            const QStringList notification{
                u"--app-name="_s + i18nc("@title the app name in notifications", "Updates"),
                u"--icon=system-software-update"_s,
                u"--action=open="_s + i18nc("@action:button", "Open Updates"),
                i18nc("@title", "Updates are ready"),
                readyMessage,
            };
            if (const QString systemdRun = QStandardPaths::findExecutable(u"systemd-run"_s); !systemdRun.isEmpty()) {
                const QString script =
                    u"[ \"$(\"$0\" \"$@\")\" = open ] && exec %1"_s.arg(openUpdatesCommand.join(u' '));
                if (capture(systemdRun,
                            QStringList{u"--user"_s, u"--collect"_s, u"--quiet"_s, u"--slice=app.slice"_s,
                                        u"--description=Updates notification"_s, u"--"_s, u"/bin/sh"_s, u"-c"_s, script,
                                        notifySend} +
                                notification)
                        .succeeded()) {
                    return;
                }
            }
            constexpr std::chrono::minutes notificationTimeoutBeforeNextPoll{4};
            if (const CommandResult result =
                    captureWithTimeout(notifySend, notification, notificationTimeoutBeforeNextPoll);
                result.succeeded() && result.out.trimmed() == "open") {
                QProcess::startDetached(openUpdatesCommand.constFirst(), openUpdatesCommand.mid(1));
            }
        }

        void checkForImageUpdate(const QDateTime &now) {
            if (!isImageMode()) {
                return;
            }
            InstallLock systemLock(systemLockPath());
            if (QString error; systemLock.tryLock(&error) != InstallLock::Result::Acquired) {
                if (!error.isEmpty()) {
                    warn(error);
                }
                return;
            }
            InstallLock imageCheckInProgressLock(imageCheckLockPath());
            if (QString error; imageCheckInProgressLock.tryLock(&error) == InstallLock::Result::Failed) {
                warn(error);
            }
            updateBootcStatusCache();
            const QString checked = systemStateDir() + "/last-image-check"_L1;
            if (within(checked, imageCheckInterval, now)) {
                return;
            }
            if (QString error; !checkImageUpdate(&error)) {
                warn(error);
                return;
            }
            updateBootcStatusCache();
            writeTimestampOrWarn(checked, now);
        }

        bool screenInhibitedByAnyone(const QList<LoginSession> &sessions, const QDateTime &now) {
            return std::ranges::any_of(sessions, [&now](const LoginSession &session) {
                return session.interactive() && session.active && session.uid != 0 &&
                    freshScreenInhibitedMarker(screenInhibitedMarkerPath(session.uid), session.uid, maxPollGap, now);
            });
        }

    } // namespace

    QString lastInstallPath(const QString &stateDir) { return stateDir + "/last-automatic-update"_L1; }

    void recordManualInstall(const bool userManager, const QDateTime &now) {
        writeTimestampOrWarn(lastInstallPath(stateDirFor(userManager)), now);
        if (QString warning; !cancelTonight(userManager, &warning)) {
            warn(warning);
        }
    }

    bool installFromTimer(const QString &lockPath, const QString &stateDir, const QString &installedPath,
                          const QDateTime &now, const std::function<ApplyResult()> &install, bool *installed,
                          QString *error) {
        *installed = false;
        InstallLock lock(lockPath);
        if (QString lockError; lock.tryLock(&lockError) != InstallLock::Result::Acquired) {
            if (lockError.isEmpty()) {
                return true;
            }
            *error = std::move(lockError);
            return false;
        }
        const QString lastFailurePath = stateDir + "/last-failed-update"_L1;
        if (within(lastFailurePath, retryAfterFailedInstall, now)) {
            return true;
        }
        const ApplyResult result = install();
        if (!result.countsAsInstalled()) {
            writeTimestampOrWarn(lastFailurePath, now);
            *error = result.error();
            return false;
        }
        QFile::remove(lastFailurePath);
        writeTimestampOrWarn(installedPath, now);
        *installed = true;
        if (!result.ok()) {
            *error = result.error();
            return false;
        }
        return true;
    }

    void notifyPendingUpdates(const QString &checkedPath, const QDateTime &now,
                              const std::function<ListResponse()> &list, const std::function<void(qsizetype)> &notify) {
        if (within(checkedPath, minTimeBetweenUpdateNotifications, now)) {
            return;
        }
        const ListResponse response = list();
        if (response.allFailed()) {
            return;
        }
        writeTimestampOrWarn(checkedPath, now);
        if (const qsizetype count = response.updates.size(); count > 0) {
            notify(count);
        }
    }

    QJsonObject ScheduleStatus::toJson() const {
        QJsonObject object{{u"scheduled"_s, scheduled}};
        if (next != 0) {
            object.insert(u"next"_s, next);
        }
        if (partial) {
            object.insert(u"partial"_s, true);
        }
        return object;
    }

    ScheduleStatus combineTonightStatus(const ScheduleStatus &system, const std::optional<ScheduleStatus> &user) {
        if (!user) {
            return system;
        }
        ScheduleStatus status;
        status.scheduled = system.scheduled && user->scheduled;
        status.partial = system.scheduled != user->scheduled;
        if (status.scheduled) {
            status.next = system.next != 0 && user->next != 0 ? std::min(system.next, user->next)
                                                              : std::max(system.next, user->next);
        }
        return status;
    }

    bool tonightAlreadyInstalled(const std::optional<QDateTime> &installed, const std::optional<QDateTime> &scheduled) {
        return installed && scheduled && *installed >= *scheduled;
    }

    ScheduleStatus parseTimerShow(const QString &output) {
        ScheduleStatus status;
        for (const QStringList lines = output.split(u'\n'); const QString &rawLine: lines) {
            const QString line = rawLine.trimmed();
            const qsizetype equals = line.indexOf(u'=');
            if (equals < 0) {
                continue;
            }
            const QString key = line.left(equals);
            QString value = line.mid(equals + 1);
            if (key == "ActiveState"_L1) {
                status.scheduled = value == "active"_L1;
            } else if (key == "NextElapseUSecRealtime"_L1) {
                if (value.startsWith(u'@')) {
                    value.remove(0, 1);
                }
                bool ok = false;
                if (const qint64 next = value.toLongLong(&ok); ok) {
                    status.next = next;
                }
            }
        }
        if (!status.scheduled) {
            status.next = 0;
        }
        return status;
    }

    ScheduleStatus tonightStatus() {
        const auto timerStatus = [](const bool userManager) {
            return parseTimerShow(
                QString::fromUtf8(systemctl(userManager,
                                            {u"show"_s, u"--timestamp=unix"_s, u"-p"_s, u"ActiveState"_s, u"-p"_s,
                                             u"NextElapseUSecRealtime"_s, tonightTimerFor(userManager) + ".timer"_L1})
                                      .out));
        };
        const ScheduleStatus system = timerStatus(false);
        if (geteuid() == 0) {
            return system;
        }
        return combineTonightStatus(system, timerStatus(true));
    }

    bool readyToInstall(const Settings &settings, const bool tonight, const bool inUse,
                        const std::chrono::seconds inactive, const bool installedRecently, const QDateTime &now,
                        QString *reason) {
        const auto no = [reason](const QString &why) {
            if (reason) {
                *reason = why;
            }
            return false;
        };
        QString start = settings.windowStart;
        QString end = settings.windowEnd;
        if (tonight) {
            if (start.isEmpty()) {
                start = tonightDefaultStart;
                end = tonightDefaultEnd;
            }
        } else {
            if (!settings.automatic) {
                return no(u"automatic updates are off"_s);
            }
            if (installedRecently) {
                return no(u"updates were installed recently"_s);
            }
        }
        if (!inWindow(now, start, end)) {
            return no(u"outside the update window"_s);
        }
        if (inUse) {
            return no(u"the device is in use"_s);
        }
        if (inactive < std::chrono::minutes(settings.inactiveMinutes)) {
            return no(u"the device was used recently"_s);
        }
        return true;
    }

    namespace {
        bool tonightInstalledSinceScheduled(const bool userManager) {
            return tonightAlreadyInstalled(readTimestamp(lastInstallPath(stateDirFor(userManager))),
                                           readTimestamp(tonightScheduledPath(userManager)));
        }

        bool installWhenInactive(const bool userManager, const Settings &settings, const bool tonight, const bool inUse,
                                 const std::optional<QDateTime> &idleSince, const QDateTime &now,
                                 const std::function<ApplyResult()> &install, QString *error) {
            const QString stateDir = stateDirFor(userManager);
            const QString installedPath = lastInstallPath(stateDir);
            const std::chrono::seconds inactive =
                ActivityTracker(stateDir + "/inactive-since"_L1, stateDir + "/last-poll"_L1)
                    .inactiveFor(inUse, now, idleSince);
            if (const bool installedRecently = within(installedPath, minTimeBetweenInstalls, now);
                !readyToInstall(settings, tonight, inUse, inactive, installedRecently, now) || !onACPower()) {
                return true;
            }
            bool installed = false;
            const bool ok = installFromTimer(userManager ? userLockPath() : systemLockPath(), stateDir, installedPath,
                                             now, install, &installed, error);
            if (tonight && installed) {
                stopTonightTimer(userManager);
            }
            return ok;
        }
    } // namespace

    bool runScheduled(const bool tonight, QString *error) {
        if (geteuid() != 0) {
            *error = i18nc("@info:shell", "run-scheduled is run by the system timers as root.");
            return false;
        }
        const Settings settings = loadSettingsOrWarn();
        const QDateTime now = QDateTime::currentDateTime();
        checkForImageUpdate(now);
        if (!tonight && !settings.automatic) {
            return true;
        }
        if (tonight && tonightInstalledSinceScheduled(false)) {
            say(i18nc("@info:status", "Updates were already installed since Apply Tonight was scheduled."));
            stopTonightTimer(false);
            return true;
        }
        QList<LoginSession> sessions;
        if (!loginSessions(&sessions, error)) {
            return false;
        }
        const bool inUse = sessionInUse(sessions).has_value() || idleInhibited(logindInhibitors(), sessions) ||
            screenInhibitedByAnyone(sessions, now);
        const auto install = [&settings] {
            say(i18nc("@info:progress", "No one is using the device. Installing updates…"));
            const ShutdownInhibitor inhibitor(
                i18nc("@info the reason shutting down waits", "Installing system updates"));
            ApplyResult result = applySystem(true, settings.offline);
            if (result.countsAsInstalled() && isImageMode()) {
                say(i18nc("@info:status", "The new system image takes effect after the next restart."));
            } else if (result.countsAsInstalled() && offlineUpdatePending()) {
                say(i18nc("@info:status", "System updates will be installed during the next restart."));
            }
            return result;
        };
        return installWhenInactive(false, settings, tonight, inUse, sessionsIdleSince(sessions), now, install, error);
    }

    bool runUser(const bool tonight, QString *error) {
        if (geteuid() == 0) {
            *error = i18nc("@info:shell", "run-user is run by the user timers.");
            return false;
        }
        const Settings settings = loadSettingsOrWarn();
        const QDateTime now = QDateTime::currentDateTime();
        QList<LoginSession> sessions;
        QString sessionsError;
        const bool sessionsKnown = loginSessions(&sessions, &sessionsError);
        const QString userName = currentUserName();
        // PowerDevil reports every inhibition as held while the session is in the background
        const bool screenInhibited =
            sessionsKnown && hasActiveSession(sessions, userName) && screenInhibitedInSession();
        writeScreenInhibitedMarker(screenInhibited, now);
        if (!tonight && !settings.automatic) {
            notifyPendingUpdates(userStateDir() + "/last-update-check"_L1, now, listUpdates, showUpdatesNotification);
            return true;
        }
        if (tonight && tonightInstalledSinceScheduled(true)) {
            say(i18nc("@info:status", "Your updates were already installed since Apply Tonight was scheduled."));
            stopTonightTimer(true);
            return true;
        }
        if (!sessionsKnown) {
            *error = std::move(sessionsError);
            return false;
        }
        const bool inUse = sessionInUse(sessions, userName).has_value() ||
            idleInhibited(logindInhibitors(), sessions, userName) || screenInhibited;
        const auto install = [] {
            say(i18nc("@info:progress", "You're not using the device. Installing your updates…"));
            const ShutdownInhibitor inhibitor(i18nc("@info the reason shutting down waits", "Installing your updates"));
            return applyUser(true);
        };
        return installWhenInactive(true, settings, tonight, inUse, sessionsIdleSince(sessions, userName), now, install,
                                   error);
    }

    bool scheduleTonight(const bool userManager, QString *error) {
        const QStringList command{userManager ? u"run-user"_s : u"run-scheduled"_s, u"--tonight"_s};
        return replaceTransientTimer(userManager, tonightTimerFor(userManager), command, error);
    }

    bool cancelTonight(const bool userManager, QString *error) {
        const QString timer = tonightTimerFor(userManager) + ".timer"_L1;
        if (const int exitCode = systemctl(userManager, {u"stop"_s, timer}).exitCode;
            exitCode != 0 && exitCode != systemctlUnitNotLoaded) {
            *error =
                i18nc("@info:status %1 is a systemd unit", "systemctl stop %1 failed (exit code %2)", timer, exitCode);
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
