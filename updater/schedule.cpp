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
#include <functional>

#include <pwd.h>
#include <unistd.h>

#include "activity.h"
#include "image.h"
#include "inhibit.h"
#include "lock.h"
#include "packagekit.h"
#include "process.h"
#include "updates.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace UltramarineUpdates {

    namespace {
        const auto tonightTimer = u"ultramarine-updates-tonight"_s;
        const auto tonightUserTimer = u"ultramarine-updates-tonight-user"_s;
        // without an update window, "tonight" means 22:00-06:00
        const auto tonightDefaultStart = u"22:00"_s;
        const auto tonightDefaultEnd = u"06:00"_s;
        // tonight's timers check every 5 minutes, like the automatic ones, and stop
        // once the updates are installed. If the device is in use all night, they
        // try again the next night.
        const auto pollCalendar = u"*:0/5"_s;

        /// how often updates install at most
        constexpr std::chrono::seconds installInterval = 20h;
        /// how often "Ask me" checks for updates and notifies
        constexpr std::chrono::seconds notifyInterval = 24h;
        /// how often image mode systems look for a new image
        constexpr std::chrono::seconds imageCheckInterval = 6h;
        /// how long to wait after a failed install (often no network) before trying again
        constexpr std::chrono::seconds retryInterval = 1h;

        void warn(const QString &message) {
            std::fprintf(stderr, "%s\n",
                         qPrintable(i18nc("@info:status %1 is a warning message", "Warning: %1", message)));
        }

        void say(const QString &message) {
            std::puts(qPrintable(message));
            std::fflush(stdout);
        }

        /// where scheduleTonight() remembers when Apply Tonight was scheduled
        const auto tonightScheduledFile = u"/tonight-scheduled"_s;

        /// development overrides, passed on to the transient units
        const QStringList forwardedEnvironment{u"ULTRAMARINE_UPDATES_CONFIG"_s, u"ULTRAMARINE_UPDATES_STATE"_s};

        int systemctl(const bool userManager, QStringList arguments) {
            if (userManager) {
                arguments.prepend(u"--user"_s);
            }
            return capture(u"systemctl"_s, arguments).exitCode;
        }

        /// whether @p unit (a service) is running right now
        bool unitRunning(const bool userManager, const QString &unit) {
            QStringList arguments{u"show"_s, u"-p"_s, u"ActiveState"_s, u"--value"_s, unit};
            if (userManager) {
                arguments.prepend(u"--user"_s);
            }
            const QString state = QString::fromUtf8(capture(u"systemctl"_s, arguments).out).trimmed();
            // a oneshot service is "activating" while it runs
            return state == "activating"_L1 || state == "active"_L1 || state == "deactivating"_L1 ||
                state == "reloading"_L1;
        }

        /// the time of Apply Tonight's schedule, in the system's or the user's state directory
        QString tonightScheduledPath(const bool userManager) {
            return (userManager ? userStateDir() : systemStateDir()) + tonightScheduledFile;
        }

        /// (re)creates a transient timer running this helper with @p arguments
        bool scheduleTransientTimer(const bool userManager, const QString &unit, const QStringList &arguments,
                                    QString *error) {
            // its unit name is taken while it runs, so leave it be. It stops the timer once it's done.
            if (unitRunning(userManager, unit + ".service"_L1)) {
                say(i18nc("@info:status", "Tonight's updates are being installed right now."));
                return true;
            }
            // replace an existing schedule. It's fine if there is none.
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
                // like ultramarine-updates-automatic.service. The user manager has no network-online.target.
                runArguments << u"--property=Wants=network-online.target"_s
                             << u"--property=After=network-online.target"_s << u"--property=TimeoutStopSec=30min"_s;
            }
            // the units start with a clean environment. A development override (passed
            // through sudo, or set for the user's half) must reach them too, or they'd
            // read the real settings and state while scheduling wrote the override's.
            for (const QString &name: forwardedEnvironment) {
                if (const QByteArray variable = name.toLatin1(); qEnvironmentVariableIsSet(variable.constData())) {
                    runArguments << u"--setenv=%1=%2"_s.arg(name, qEnvironmentVariable(variable.constData()));
                }
            }
            runArguments << u"--"_s << QCoreApplication::applicationFilePath();
            runArguments += arguments;
            // a tonight timer only installs if nothing else did since, see tonightAlreadyInstalled()
            if (QString warning;
                !writeTimestamp(tonightScheduledPath(userManager), QDateTime::currentDateTime(), &warning)) {
                warn(warning);
            }
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

        const QStringList openUpdatesCommand{u"systemsettings"_s, u"kcm_ultramarine_updates"_s};

        /**
         * shows that @p count updates are ready. "Open Updates" opens System
         * Settings > Updates.
         *
         * notify-send waits until the notification is acted on or closed, which
         * can take until the user comes back, or forever if the notification server
         * keeps it in its history. So it runs in a transient service of its own,
         * which also opens System Settings, and run-user doesn't wait for it.
         */
        void showUpdatesNotification(const QString &notifySend, const qsizetype count) {
            const QStringList notification{
                u"--app-name="_s + i18nc("@title the app name in notifications", "Updates"),
                u"--icon=system-software-update"_s,
                u"--action=open="_s + i18nc("@action:button", "Open Updates"),
                i18nc("@title", "Updates are ready"),
                i18ncp("@info", "1 update is ready to install.", "%1 updates are ready to install.", count),
            };
            if (const QString systemdRun = QStandardPaths::findExecutable(u"systemd-run"_s); !systemdRun.isEmpty()) {
                // the texts are passed as arguments, never as part of the script
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
            // without systemd-run, wait a few minutes at most, so the next poll isn't held up
            constexpr std::chrono::minutes notificationTimeout{4};
            if (const CommandResult result = captureWithTimeout(notifySend, notification, notificationTimeout);
                result.succeeded() && result.out.trimmed() == "open") {
                QProcess::startDetached(openUpdatesCommand.constFirst(), openUpdatesCommand.mid(1));
            }
        }

        /**
         * image mode: has bootc (or rpm-ostree) look for a new image, which needs
         * root. Without this, the KCM and notifications, which run as the user,
         * would only see updates someone checked for by hand.
         */
        void checkForImageUpdate(const QDateTime &now) {
            if (!isImageMode()) {
                return;
            }
            // skipped while an install runs, which checks and rewrites the cache itself.
            // The system lock is only taken for a moment to find out, and a manual
            // Apply waits that long for it (see applyCommand()). The cache has a lock
            // of its own (see updateBootcStatusCache()).
            {
                InstallLock probe(systemLockPath());
                if (QString error; probe.tryLock(&error) != InstallLock::Result::Acquired) {
                    if (!error.isEmpty()) {
                        warn(error);
                    }
                    return;
                }
            }
            // cheap, and keeps what the KCM shows current after restarting or a manual upgrade
            updateBootcStatusCache();
            const QString checked = systemStateDir() + "/last-image-check"_L1;
            if (within(checked, imageCheckInterval, now)) {
                return;
            }
            // failures (usually no network) are retried on the next run
            if (QString error; !checkImageUpdate(&error)) {
                warn(error);
                return;
            }
            // so the listing sees what the check found
            updateBootcStatusCache();
            if (QString error; !writeTimestamp(checked, now, &error)) {
                warn(error);
            }
        }

        /**
         * whether the user timer of someone with an active interactive session noted
         * that an app keeps their screen on (see writeScreenInhibitedMarker()). The
         * system timer can't ask PowerDevil itself, which is on each user's session bus.
         */
        bool screenInhibitedByAnyone(const QList<LoginSession> &sessions, const QDateTime &now) {
            return std::ranges::any_of(sessions, [&now](const LoginSession &session) {
                return session.interactive() && session.active && session.uid != 0 &&
                    freshScreenInhibitedMarker(screenInhibitedMarkerPath(session.uid), session.uid, maxPollGap, now);
            });
        }

    } // namespace

    bool installFromTimer(const QString &lockPath, const QString &stateDir, const QString &installedPath,
                          const QDateTime &now, const std::function<ApplyResult()> &install, bool *installed,
                          QString *error) {
        *installed = false;
        InstallLock lock(lockPath);
        if (QString lockError; lock.tryLock(&lockError) != InstallLock::Result::Acquired) {
            // busy: a manual install or the other timer is running, so it'll be retried
            if (!lockError.isEmpty()) {
                *error = lockError;
                return false;
            }
            return true;
        }
        const QString failed = stateDir + "/last-failed-update"_L1;
        if (within(failed, retryInterval, now)) {
            return true;
        }
        const ApplyResult result = install();
        if (!result.installed()) {
            if (QString warning; !writeTimestamp(failed, now, &warning)) {
                warn(warning);
            }
            *error = result.error();
            return false;
        }
        QFile::remove(failed);
        if (QString warning; !writeTimestamp(installedPath, now, &warning)) {
            warn(warning);
        }
        *installed = true;
        // the rest is installed, so only the failed sources wait for the next regular run
        if (!result.ok()) {
            *error = result.error();
            return false;
        }
        return true;
    }

    void notifyPendingUpdates(const QString &checkedPath, const QDateTime &now,
                              const std::function<ListResponse()> &list, const std::function<void(qsizetype)> &notify) {
        if (within(checkedPath, notifyInterval, now)) {
            return;
        }
        const ListResponse response = list();
        // e.g. no network yet right after logging in. The next run tries again.
        if (response.allFailed()) {
            return;
        }
        if (QString error; !writeTimestamp(checkedPath, now, &error)) {
            warn(error);
        }
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
        const auto timerStatus = [](const bool userManager, const QString &timer) {
            QStringList arguments{u"show"_s,
                                  u"--timestamp=unix"_s,
                                  u"-p"_s,
                                  u"ActiveState"_s,
                                  u"-p"_s,
                                  u"NextElapseUSecRealtime"_s,
                                  timer + ".timer"_L1};
            if (userManager) {
                arguments.prepend(u"--user"_s);
            }
            return parseTimerShow(QString::fromUtf8(capture(u"systemctl"_s, arguments).out));
        };
        const ScheduleStatus system = timerStatus(false, tonightTimer);
        // root has no user half. The user's goes away when they log out.
        if (geteuid() == 0) {
            return system;
        }
        return combineTonightStatus(system, timerStatus(true, tonightUserTimer));
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

    bool runScheduled(const bool tonight, QString *error) {
        if (geteuid() != 0) {
            *error = i18nc("@info:shell", "run-scheduled is run by the system timers as root.");
            return false;
        }
        Settings settings;
        if (!loadSettings(&settings, error)) {
            return false;
        }
        const QDateTime now = QDateTime::currentDateTime();
        // also with automatic updates off, so "Ask me" notifies about new images
        checkForImageUpdate(now);
        if (!tonight && !settings.automatic) {
            return true;
        }
        const QString stateDir = systemStateDir();
        const QString installed = stateDir + "/last-automatic-update"_L1;
        if (tonight && tonightAlreadyInstalled(readTimestamp(installed), readTimestamp(tonightScheduledPath(false)))) {
            say(i18nc("@info:status", "Updates were already installed since Apply Tonight was scheduled."));
            systemctl(false, {u"stop"_s, tonightTimer + ".timer"_L1});
            return true;
        }
        QList<LoginSession> sessions;
        if (!loginSessions(&sessions, error)) {
            return false;
        }
        const bool inUse = sessionInUse(sessions).has_value() || idleInhibited(logindInhibitors(), sessions) ||
            screenInhibitedByAnyone(sessions, now);
        const std::chrono::seconds inactive =
            ActivityTracker(stateDir + "/inactive-since"_L1, stateDir + "/last-poll"_L1)
                .inactiveFor(inUse, now, sessionsIdleSince(sessions));
        if (!readyToInstall(settings, tonight, inUse, inactive, within(installed, installInterval, now), now) ||
            !onACPower()) {
            return true;
        }

        const auto install = [&settings] {
            say(i18nc("@info:progress", "No one is using the device. Installing updates…"));
            const ShutdownInhibitor inhibitor(
                i18nc("@info the reason shutting down waits", "Installing system updates"));
            ApplyResult result = applySystem(true, settings.offline);
            if (result.installed() && isImageMode()) {
                say(i18nc("@info:status", "The new system image takes effect after the next restart."));
            } else if (result.installed() && offlineUpdatePending()) {
                say(i18nc("@info:status", "System updates will be installed during the next restart."));
            }
            return result;
        };
        bool installedNow = false;
        const bool ok = installFromTimer(systemLockPath(), stateDir, installed, now, install, &installedNow, error);
        // also when some sources failed, which are retried on the next regular run
        if (tonight && installedNow) {
            systemctl(false, {u"stop"_s, tonightTimer + ".timer"_L1});
        }
        return ok;
    }

    bool runUser(const bool tonight, QString *error) {
        if (geteuid() == 0) {
            *error = i18nc("@info:shell", "run-user is run by the user timers.");
            return false;
        }
        Settings settings;
        if (!loadSettings(&settings, error)) {
            return false;
        }
        const QDateTime now = QDateTime::currentDateTime();
        QList<LoginSession> sessions;
        QString sessionsError;
        const bool sessionsKnown = loginSessions(&sessions, &sessionsError);
        const QString userName = currentUserName();
        // also with automatic updates off, for the system's Apply Tonight. PowerDevil
        // reports every inhibition as held in a session that isn't in the foreground,
        // so it's only asked while one of ours is.
        const bool screenInhibited =
            sessionsKnown && hasActiveSession(sessions, userName) && screenInhibitedInSession();
        writeScreenInhibitedMarker(screenInhibited, now);
        if (!tonight && !settings.automatic) {
            notifyPendingUpdates(userStateDir() + "/last-update-check"_L1, now, listUpdates, [](const qsizetype count) {
                if (const QString notifySend = QStandardPaths::findExecutable(u"notify-send"_s);
                    !notifySend.isEmpty()) {
                    showUpdatesNotification(notifySend, count);
                } else {
                    say(i18ncp("@info", "1 update is ready to install.", "%1 updates are ready to install.", count));
                }
            });
            return true;
        }

        const QString stateDir = userStateDir();
        const QString installed = stateDir + "/last-automatic-update"_L1;
        if (tonight && tonightAlreadyInstalled(readTimestamp(installed), readTimestamp(tonightScheduledPath(true)))) {
            say(i18nc("@info:status", "Your updates were already installed since Apply Tonight was scheduled."));
            systemctl(true, {u"stop"_s, tonightUserTimer + ".timer"_L1});
            return true;
        }
        if (!sessionsKnown) {
            *error = sessionsError;
            return false;
        }
        const bool inUse = sessionInUse(sessions, userName).has_value() ||
            idleInhibited(logindInhibitors(), sessions, userName) || screenInhibited;
        const std::chrono::seconds inactive =
            ActivityTracker(stateDir + "/inactive-since"_L1, stateDir + "/last-poll"_L1)
                .inactiveFor(inUse, now, sessionsIdleSince(sessions, userName));
        if (!readyToInstall(settings, tonight, inUse, inactive, within(installed, installInterval, now), now) ||
            !onACPower()) {
            return true;
        }

        const auto install = [] {
            say(i18nc("@info:progress", "You're not using the device. Installing your updates…"));
            const ShutdownInhibitor inhibitor(i18nc("@info the reason shutting down waits", "Installing your updates"));
            return applyUser(true);
        };
        bool installedNow = false;
        const bool ok = installFromTimer(userLockPath(), stateDir, installed, now, install, &installedNow, error);
        if (tonight && installedNow) {
            systemctl(true, {u"stop"_s, tonightUserTimer + ".timer"_L1});
        }
        return ok;
    }

    bool scheduleTonight(const bool userManager, QString *error) {
        if (userManager) {
            return scheduleTransientTimer(true, tonightUserTimer, {u"run-user"_s, u"--tonight"_s}, error);
        }
        return scheduleTransientTimer(false, tonightTimer, {u"run-scheduled"_s, u"--tonight"_s}, error);
    }

    bool cancelTonight(const bool userManager, QString *error) {
        const QString timer = (userManager ? tonightUserTimer : tonightTimer) + ".timer"_L1;
        // 5: the transient timer isn't loaded, so nothing was scheduled
        if (const int exitCode = systemctl(userManager, {u"stop"_s, timer}); exitCode != 0 && exitCode != 5) {
            *error =
                i18nc("@info:status %1 is a systemd unit", "systemctl stop %1 failed (exit code %2)", timer, exitCode);
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
