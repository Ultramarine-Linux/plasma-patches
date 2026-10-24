// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "activity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    bool LoginSession::inUse() const {
        if (sessionClass != "user"_L1 || !active || locked || idle) {
            return false;
        }
        return type == "wayland"_L1 || type == "x11"_L1 || type == "mir"_L1 || type == "tty"_L1;
    }

    QList<LoginSession> parseShowSessions(const QString &output) {
        QList<LoginSession> sessions;
        std::optional<LoginSession> current;
        const auto flush = [&sessions, &current] {
            if (current && !current->id.isEmpty()) {
                sessions.append(*current);
            }
            current.reset();
        };
        for (const QStringList lines = output.split(u'\n'); const QString &rawLine: lines) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty()) {
                flush();
                continue;
            }
            const qsizetype equals = line.indexOf(u'=');
            if (equals < 0) {
                continue;
            }
            if (!current) {
                current.emplace();
            }
            const QString key = line.left(equals);
            const QString value = line.mid(equals + 1);
            if (key == "Id"_L1) {
                current->id = value;
            } else if (key == "Name"_L1) {
                current->name = value;
            } else if (key == "Class"_L1) {
                current->sessionClass = value;
            } else if (key == "Type"_L1) {
                current->type = value;
            } else if (key == "Active"_L1) {
                current->active = value == "yes"_L1;
            } else if (key == "LockedHint"_L1) {
                current->locked = value == "yes"_L1;
            } else if (key == "IdleHint"_L1) {
                current->idle = value == "yes"_L1;
            }
        }
        flush();
        return sessions;
    }

    std::optional<LoginSession> sessionInUse(const QList<LoginSession> &sessions, const QString &userName) {
        for (const LoginSession &session: sessions) {
            if (!userName.isEmpty() && session.name != userName) {
                continue;
            }
            if (session.inUse()) {
                return session;
            }
        }
        return std::nullopt;
    }

    bool loginSessions(QList<LoginSession> *sessions, QString *error) {
        sessions->clear();
        QString list;
        if (!commandOutput(u"loginctl"_s, {u"list-sessions"_s, u"--no-legend"_s}, &list, error)) {
            return false;
        }
        QStringList arguments{u"show-session"_s};
        for (const QStringList lines = list.split(u'\n'); const QString &line: lines) {
            if (const QStringList fields = line.simplified().split(u' ', Qt::SkipEmptyParts); !fields.isEmpty()) {
                arguments.append(fields.constFirst());
            }
        }
        if (arguments.size() == 1) {
            return true;
        }
        for (const auto property: {"Id", "Name", "Class", "Type", "Active", "LockedHint", "IdleHint"}) {
            arguments << u"-p"_s << QString::fromLatin1(property);
        }
        QString output;
        if (!commandOutput(u"loginctl"_s, arguments, &output, error)) {
            return false;
        }
        *sessions = parseShowSessions(output);
        return true;
    }

    bool onACPower() {
        // Treat a missing systemd-ac-power as AC power rather than never updating.
        const CommandResult result = capture(QStandardPaths::findExecutable(u"systemd-ac-power"_s), {});
        return !result.started || result.exitCode == 0;
    }

    std::optional<QDateTime> readTimestamp(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        bool ok = false;
        const qint64 seconds = file.readAll().trimmed().toLongLong(&ok);
        if (!ok) {
            return std::nullopt;
        }
        return QDateTime::fromSecsSinceEpoch(seconds);
    }

    bool writeTimestamp(const QString &path, const QDateTime &time, QString *error) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            if (error) {
                *error = u"could not create %1"_s.arg(QFileInfo(path).absolutePath());
            }
            return false;
        }
        if (QFile file(path); !file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(QByteArray::number(time.toSecsSinceEpoch()) + '\n') < 0) {
            if (error) {
                *error = u"%1: %2"_s.arg(path, file.errorString());
            }
            return false;
        }
        return true;
    }

    bool within(const QString &path, const std::chrono::seconds duration, const QDateTime &now) {
        const std::optional<QDateTime> time = readTimestamp(path);
        if (!time || *time > now) {
            return false;
        }
        return time->secsTo(now) < duration.count();
    }

    std::chrono::seconds ActivityTracker::inactiveFor(const bool inUse, const QDateTime &now) const {
        if (inUse) {
            QFile::remove(m_path);
            return std::chrono::seconds(0);
        }
        const std::optional<QDateTime> since = readTimestamp(m_path);
        if (!since || *since > now) {
            writeTimestamp(m_path, now, nullptr);
            return std::chrono::seconds(0);
        }
        return std::chrono::seconds(since->secsTo(now));
    }

    QString systemStateDir() {
        if (const QString dir = qEnvironmentVariable("ULTRAMARINE_UPDATES_STATE"); !dir.isEmpty()) {
            return dir;
        }
        return u"/var/lib/ultramarine-updates"_s;
    }

    QString userStateDir() {
        QString stateHome = qEnvironmentVariable("XDG_STATE_HOME");
        if (stateHome.isEmpty()) {
            stateHome = QDir::homePath() + "/.local/state"_L1;
        }
        return stateHome + "/ultramarine-updates"_L1;
    }

} // namespace UltramarineUpdates
