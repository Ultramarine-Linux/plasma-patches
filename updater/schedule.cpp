// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "schedule.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QStandardPaths>

#include <cstdio>

#include <pwd.h>
#include <unistd.h>

#include "activity.h"
#include "process.h"
#include "updates.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace UltramarineUpdates {

    namespace {
        const auto tonightTimer = u"ultramarine-updates-tonight"_s;
        const auto tonightUserTimer = u"ultramarine-updates-tonight-user"_s;
        // Without an update window, "tonight" means 22:00-06:00.
        const auto tonightDefaultStart = u"22:00"_s;
        const auto tonightDefaultEnd = u"06:00"_s;
        // Tonight's timers check every 5 minutes, like the automatic ones, and stop
        // once the updates are installed. If the device is in use all night, they
        // try again the next night.
        const auto pollCalendar = u"*:0/5"_s;

        /// How often updates install at most.
        constexpr std::chrono::seconds installInterval = 20h;
        /// How often "Ask me" checks for updates and notifies.
        constexpr std::chrono::seconds notifyInterval = 24h;

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

        /// (Re)creates a transient timer running this helper with @p arguments.
        bool scheduleTransientTimer(const bool userManager, const QString &unit, const QStringList &arguments,
                                    QString *error) {
            // Replace an existing schedule; it's fine if there is none.
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

        /// Shows a notification if updates are available, at most once a day.
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
            // notify-send waits for an action or for the notification to close.
            const CommandResult result =
                capture(notifySend,
                        {u"--app-name="_s + i18n("Updates"), u"--icon=system-software-update"_s,
                         u"--action=open="_s + i18n("Open Updates"), i18n("Updates are ready"),
                         i18np("1 update is ready to install.", "%1 updates are ready to install.", count)});
            if (result.succeeded() && result.out.trimmed() == "open") {
                QProcess::startDetached(u"systemsettings"_s, {u"kcm_ultramarine_updates"_s});
            }
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
        if (!tonight && !settings.automatic) {
            return true;
        }
        QList<LoginSession> sessions;
        if (!loginSessions(&sessions, error)) {
            return false;
        }
        const bool inUse = sessionInUse(sessions).has_value();
        const QDateTime now = QDateTime::currentDateTime();
        const std::chrono::seconds inactive =
            ActivityTracker(systemStateDir() + "/inactive-since"_L1).inactiveFor(inUse, now);
        const QString installed = systemStateDir() + "/last-automatic-update"_L1;
        if (!readyToInstall(settings, tonight, inUse, inactive, within(installed, installInterval, now), now) ||
            !onACPower()) {
            return true;
        }

        say(u"No one is using the device; installing updates."_s);
        bool updated = false;
        if (!applySystem(true, settings.offline, &updated, error)) {
            return false;
        }
        if (settings.offline) {
            say(u"System updates will be installed during the next restart."_s);
        }
        if (QString warning; !writeTimestamp(installed, now, &warning)) {
            warn(warning);
        }
        if (tonight) {
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
        const std::chrono::seconds inactive =
            ActivityTracker(userStateDir() + "/inactive-since"_L1).inactiveFor(inUse, now);
        const QString installed = userStateDir() + "/last-automatic-update"_L1;
        if (!readyToInstall(settings, tonight, inUse, inactive, within(installed, installInterval, now), now)) {
            return true;
        }

        say(u"You're not using the device; installing your updates."_s);
        if (!applyUser(true, error)) {
            return false;
        }
        if (QString warning; !writeTimestamp(installed, now, &warning)) {
            warn(warning);
        }
        if (tonight) {
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

    void cancelTonight(const bool userManager) {
        systemctl(userManager, {u"stop"_s, (userManager ? tonightUserTimer : tonightTimer) + ".timer"_L1});
    }

} // namespace UltramarineUpdates
