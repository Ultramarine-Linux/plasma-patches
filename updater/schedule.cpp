// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "schedule.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include <cstdio>
#include <functional>

#include <pwd.h>
#include <unistd.h>

#include "activity.h"
#include "image.h"
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

        void warn(const QString &message) { std::fprintf(stderr, "warning: %s\n", qPrintable(message)); }

        void say(const QString &message) {
            std::puts(qPrintable(message));
            std::fflush(stdout);
        }

        int systemctl(const bool userManager, QStringList arguments) {
            if (userManager) {
                arguments.prepend(u"--user"_s);
            }
            return capture(u"systemctl"_s, arguments).exitCode;
        }

        /// (re)creates a transient timer running this helper with @p arguments
        bool scheduleTransientTimer(const bool userManager, const QString &unit, const QStringList &arguments,
                                    QString *error) {
            // replace an existing schedule. It's fine if there is none.
            systemctl(userManager, {u"stop"_s, unit + ".timer"_L1});
            systemctl(userManager, {u"reset-failed"_s, unit + ".service"_L1});

            QStringList runArguments;
            if (userManager) {
                runArguments.append(u"--user"_s);
            }
            runArguments << u"--unit="_s + unit << u"--on-calendar="_s + pollCalendar
                         << u"--timer-property=AccuracySec=1min"_s << u"--description=Install updates tonight"_s
                         << u"--property=Nice=19"_s << u"--property=IOSchedulingClass=idle"_s << u"--"_s
                         << QCoreApplication::applicationFilePath();
            runArguments += arguments;
            if (const CommandResult result = capture(u"systemd-run"_s, runArguments); !result.succeeded()) {
                *error = u"systemd-run failed: %1"_s.arg(QString::fromUtf8(result.out + result.err).trimmed());
                return false;
            }
            return true;
        }

        QString currentUserName() {
            const passwd *entry = getpwuid(getuid()); // NOLINT(concurrency-mt-unsafe): single-threaded CLI
            return entry ? QString::fromLocal8Bit(entry->pw_name) : QString();
        }

        /// shows a notification if updates are available, at most once a day.
        /// "Open Updates" opens System Settings > Updates.
        void notifyPendingUpdates(const QDateTime &now) {
            const QString checked = userStateDir() + "/last-update-check"_L1;
            if (within(checked, notifyInterval, now)) {
                return;
            }
            if (QString error; !writeTimestamp(checked, now, &error)) {
                warn(error);
            }
            const qsizetype count = listUpdates().updates.size();
            if (count == 0) {
                return;
            }
            const QString notifySend = QStandardPaths::findExecutable(u"notify-send"_s);
            if (notifySend.isEmpty()) {
                say(u"%1 updates are available."_s.arg(count));
                return;
            }
            // notify-send waits for an action or for the notification to close
            const CommandResult result =
                capture(notifySend,
                        {u"--app-name="_s + i18n("Updates"), u"--icon=system-software-update"_s,
                         u"--action=open="_s + i18n("Open Updates"), i18n("Updates are ready"),
                         i18np("1 update is ready to install.", "%1 updates are ready to install.", count)});
            if (result.succeeded() && result.out.trimmed() == "open") {
                QProcess::startDetached(u"systemsettings"_s, {u"kcm_ultramarine_updates"_s});
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
            {
                // skipped while an install runs, which rewrites the cache itself once it's done.
                // Only held for the cache, so a manual Apply isn't turned away during the check.
                InstallLock lock(systemLockPath());
                if (QString error; lock.tryLock(&error) != InstallLock::Result::Acquired) {
                    if (!error.isEmpty()) {
                        warn(error);
                    }
                    return;
                }
                // cheap, and keeps what the KCM shows current after restarting or a manual upgrade
                updateBootcStatusCache();
            }
            const QString checked = systemStateDir() + "/last-image-check"_L1;
            if (within(checked, imageCheckInterval, now)) {
                return;
            }
            // failures (usually no network) are retried on the next run
            if (QString error; !checkImageUpdate(&error)) {
                warn(error);
                return;
            }
            if (QString error; !writeTimestamp(checked, now, &error)) {
                warn(error);
            }
        }

        /**
         * runs @p install for a timer once it's ready, unless an install is already
         * running or the last one failed less than retryInterval ago: tonight's
         * timers poll every 5 minutes, and a failure shouldn't retry all night.
         * Remembers the time of a successful install in @p installedPath, and
         * sets @p installed if it installed.
         */
        bool installFromTimer(const QString &lockPath, const QString &stateDir, const QString &installedPath,
                              const QDateTime &now, const std::function<bool(QString *)> &install, bool *installed,
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
            if (!install(error)) {
                if (QString warning; !writeTimestamp(failed, now, &warning)) {
                    warn(warning);
                }
                return false;
            }
            QFile::remove(failed);
            if (QString warning; !writeTimestamp(installedPath, now, &warning)) {
                warn(warning);
            }
            *installed = true;
            return true;
        }
    } // namespace

    QJsonObject ScheduleStatus::toJson() const {
        QJsonObject object{{u"scheduled"_s, scheduled}};
        if (next != 0) {
            object.insert(u"next"_s, next);
        }
        return object;
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
        const CommandResult result = capture(u"systemctl"_s,
                                             {u"show"_s, u"--timestamp=unix"_s, u"-p"_s, u"ActiveState"_s, u"-p"_s,
                                              u"NextElapseUSecRealtime"_s, tonightTimer + ".timer"_L1});
        return parseTimerShow(QString::fromUtf8(result.out));
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
            *error = u"run-scheduled is run by the system timers as root"_s;
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
        QList<LoginSession> sessions;
        if (!loginSessions(&sessions, error)) {
            return false;
        }
        const bool inUse = sessionInUse(sessions).has_value();
        const QString stateDir = systemStateDir();
        const std::chrono::seconds inactive =
            ActivityTracker(stateDir + "/inactive-since"_L1, stateDir + "/last-poll"_L1).inactiveFor(inUse, now);
        const QString installed = stateDir + "/last-automatic-update"_L1;
        if (!readyToInstall(settings, tonight, inUse, inactive, within(installed, installInterval, now), now) ||
            !onACPower()) {
            return true;
        }

        const auto install = [&settings](QString *installError) {
            say(u"No one is using the device; installing updates."_s);
            bool updated = false;
            if (!applySystem(true, settings.offline, &updated, installError)) {
                return false;
            }
            if (isImageMode()) {
                say(u"The new system image takes effect after the next restart."_s);
            } else if (offlineUpdatePending()) {
                say(u"System updates will be installed during the next restart."_s);
            }
            return true;
        };
        bool installedNow = false;
        if (!installFromTimer(systemLockPath(), stateDir, installed, now, install, &installedNow, error)) {
            return false;
        }
        if (tonight && installedNow) {
            systemctl(false, {u"stop"_s, tonightTimer + ".timer"_L1});
        }
        return true;
    }

    bool runUser(const bool tonight, QString *error) {
        if (geteuid() == 0) {
            *error = u"run-user is run by the user timers"_s;
            return false;
        }
        Settings settings;
        if (!loadSettings(&settings, error)) {
            return false;
        }
        const QDateTime now = QDateTime::currentDateTime();
        if (!tonight && !settings.automatic) {
            notifyPendingUpdates(now);
            return true;
        }

        QList<LoginSession> sessions;
        if (!loginSessions(&sessions, error)) {
            return false;
        }
        const bool inUse = sessionInUse(sessions, currentUserName()).has_value();
        const QString stateDir = userStateDir();
        const std::chrono::seconds inactive =
            ActivityTracker(stateDir + "/inactive-since"_L1, stateDir + "/last-poll"_L1).inactiveFor(inUse, now);
        const QString installed = stateDir + "/last-automatic-update"_L1;
        if (!readyToInstall(settings, tonight, inUse, inactive, within(installed, installInterval, now), now) ||
            !onACPower()) {
            return true;
        }

        const auto install = [](QString *installError) {
            say(u"You're not using the device; installing your updates."_s);
            return applyUser(true, installError);
        };
        bool installedNow = false;
        if (!installFromTimer(userLockPath(), stateDir, installed, now, install, &installedNow, error)) {
            return false;
        }
        if (tonight && installedNow) {
            systemctl(true, {u"stop"_s, tonightUserTimer + ".timer"_L1});
        }
        return true;
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
            *error = u"systemctl stop %1 failed (exit code %2)"_s.arg(timer).arg(exitCode);
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
