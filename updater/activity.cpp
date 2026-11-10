// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "activity.h"

#include <KLocalizedString>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cstdio>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "inhibit.h"
#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        /// logind and PowerDevil answer right away. Don't hang a timer run if they don't.
        constexpr int dbusTimeoutMs = 10'000;
        const auto screenInhibitedMarker = u"/ultramarine-updates/screen-inhibited"_s;
        /// a timestamp and a newline
        constexpr qint64 maxMarkerSize = 32;
        /// more than any timestamp we write, so a broken file isn't read whole
        constexpr qint64 maxTimestampSize = 64;

        /// closes a file descriptor when it goes out of scope
        struct FileDescriptor {
            explicit FileDescriptor(const int descriptor) : fd(descriptor) {}
            ~FileDescriptor() {
                if (fd >= 0) {
                    close(fd);
                }
            }
            FileDescriptor(const FileDescriptor &) = delete;
            FileDescriptor &operator=(const FileDescriptor &) = delete;

            int fd;
        };
    } // namespace

    bool LoginSession::interactive() const {
        return sessionClass == "user"_L1 &&
            (type == "wayland"_L1 || type == "x11"_L1 || type == "mir"_L1 || type == "tty"_L1);
    }

    bool LoginSession::inUse() const { return interactive() && active && !locked && !idle; }

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
            } else if (key == "User"_L1) {
                current->uid = value.toUInt();
            } else if (key == "IdleSinceHint"_L1) {
                // microseconds since the epoch, 0 if never set. loginctl doesn't format it.
                bool ok = false;
                if (const qint64 usec = value.toLongLong(&ok); ok && usec > 0) {
                    current->idleSince = QDateTime::fromMSecsSinceEpoch(usec / 1000);
                }
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
        for (const auto property:
             {"Id", "Name", "User", "Class", "Type", "Active", "LockedHint", "IdleHint", "IdleSinceHint"}) {
            arguments << u"-p"_s << QString::fromLatin1(property);
        }
        QString output;
        if (!commandOutput(u"loginctl"_s, arguments, &output, error)) {
            return false;
        }
        *sessions = parseShowSessions(output);
        return true;
    }

    std::optional<QDateTime> sessionsIdleSince(const QList<LoginSession> &sessions, const QString &userName) {
        std::optional<QDateTime> since;
        for (const LoginSession &session: sessions) {
            if ((!userName.isEmpty() && session.name != userName) || !session.interactive() || !session.active) {
                continue;
            }
            if (!session.idle || !session.idleSince) {
                return std::nullopt;
            }
            if (!since || *session.idleSince > *since) {
                since = session.idleSince;
            }
        }
        return since;
    }

    bool hasActiveSession(const QList<LoginSession> &sessions, const QString &userName) {
        return std::ranges::any_of(sessions, [&userName](const LoginSession &session) {
            return session.name == userName && session.interactive() && session.active;
        });
    }

    bool idleInhibited(const QList<Inhibitor> &inhibitors, const QList<LoginSession> &sessions,
                       const QString &userName) {
        QSet<uint> owners;
        for (const LoginSession &session: sessions) {
            if (session.interactive() && session.uid != 0 && (userName.isEmpty() || session.name == userName)) {
                owners.insert(session.uid);
            }
        }
        return std::ranges::any_of(inhibitors, [&owners](const Inhibitor &inhibitor) {
            // PowerDevil blocks logind's handling of power keys and the lid, which isn't about the screen
            return inhibitor.mode == "block"_L1 && inhibitor.uid != 0 && owners.contains(inhibitor.uid) &&
                inhibitor.who != "PowerDevil"_L1 && inhibitor.who != QLatin1StringView(inhibitorName) &&
                inhibitor.what.split(u':').contains("idle"_L1);
        });
    }

    QList<Inhibitor> logindInhibitors() {
        const QDBusMessage call =
            QDBusMessage::createMethodCall(u"org.freedesktop.login1"_s, u"/org/freedesktop/login1"_s,
                                           u"org.freedesktop.login1.Manager"_s, u"ListInhibitors"_s);
        const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, dbusTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return {};
        }
        // a(ssssuu), read by hand rather than registering a metatype for it
        QList<Inhibitor> inhibitors;
        const auto argument = reply.arguments().constFirst().value<QDBusArgument>();
        argument.beginArray();
        while (!argument.atEnd()) {
            Inhibitor inhibitor;
            argument.beginStructure();
            argument >> inhibitor.what >> inhibitor.who >> inhibitor.why >> inhibitor.mode >> inhibitor.uid >>
                inhibitor.pid;
            argument.endStructure();
            inhibitors.append(inhibitor);
        }
        argument.endArray();
        return inhibitors;
    }

    bool screenInhibitedInSession() {
        // kscreenlocker's org.freedesktop.ScreenSaver.Inhibit adds a ChangeScreenSettings
        // inhibition here (PowerDevil::PolicyAgent::RequiredPolicy)
        constexpr uint changeScreenSettings = 4;
        QDBusMessage call = QDBusMessage::createMethodCall(
            u"org.kde.Solid.PowerManagement.PolicyAgent"_s, u"/org/kde/Solid/PowerManagement/PolicyAgent"_s,
            u"org.kde.Solid.PowerManagement.PolicyAgent"_s, u"HasInhibition"_s);
        call << changeScreenSettings;
        const QDBusReply<bool> reply = QDBusConnection::sessionBus().call(call, QDBus::Block, dbusTimeoutMs);
        return reply.isValid() && reply.value();
    }

    QString screenInhibitedMarkerPath() {
        QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtimeDir.isEmpty()) {
            runtimeDir = u"/run/user/%1"_s.arg(getuid());
        }
        return runtimeDir + screenInhibitedMarker;
    }

    QString screenInhibitedMarkerPath(const uint uid) { return u"/run/user/%1"_s.arg(uid) + screenInhibitedMarker; }

    void writeScreenInhibitedMarker(const bool inhibited, const QDateTime &now) {
        const QString path = screenInhibitedMarkerPath();
        if (!inhibited) {
            QFile::remove(path);
            return;
        }
        if (QString error; !writeTimestamp(path, now, &error)) {
            std::fprintf(stderr, "warning: %s\n", qPrintable(error));
        }
    }

    bool freshScreenInhibitedMarker(const QString &path, const uint uid, const std::chrono::seconds maxAge,
                                    const QDateTime &now) {
        const auto owned = [uid](const int fd, const bool directory) {
            struct stat info{};
            return fstat(fd, &info) == 0 && info.st_uid == uid &&
                (directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode) && info.st_size <= maxMarkerSize);
        };
        // the user could swap in symlinks (for the file or its directory), FIFOs or
        // devices, so only open what's checked, by descriptor, without following links
        const QFileInfo info(path);
        const FileDescriptor directory(
            open(QFile::encodeName(info.path()).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (directory.fd < 0 || !owned(directory.fd, true)) {
            return false;
        }
        const QByteArray name = QFile::encodeName(info.fileName());
        if (struct stat entry{};
            fstatat(directory.fd, name.constData(), &entry, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(entry.st_mode)) {
            return false;
        }
        const FileDescriptor file(
            openat(directory.fd, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC));
        if (file.fd < 0 || !owned(file.fd, false)) {
            return false;
        }
        std::array<char, maxMarkerSize> buffer{};
        const ssize_t size = read(file.fd, buffer.data(), buffer.size());
        if (size <= 0) {
            return false;
        }
        const std::optional<QDateTime> time = parseTimestamp(QByteArray(buffer.data(), size));
        return time && *time <= now && time->secsTo(now) < maxAge.count();
    }

    bool onACPower() {
        // treat a missing systemd-ac-power as AC power rather than never updating
        const CommandResult result = capture(QStandardPaths::findExecutable(u"systemd-ac-power"_s), {});
        return !result.started || result.exitCode == 0;
    }

    std::optional<QDateTime> parseTimestamp(const QByteArray &text) {
        bool ok = false;
        const qint64 seconds = text.trimmed().toLongLong(&ok);
        if (!ok) {
            return std::nullopt;
        }
        // out of QDateTime's range, which would compare as long ago and never expire
        QDateTime time = QDateTime::fromSecsSinceEpoch(seconds);
        if (!time.isValid()) {
            return std::nullopt;
        }
        return time;
    }

    std::optional<QDateTime> readTimestamp(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        return parseTimestamp(file.read(maxTimestampSize));
    }

    bool writeTimestamp(const QString &path, const QDateTime &time, QString *error) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            if (error) {
                *error = i18nc("@info:status %1 is a directory", "Could not create %1", QFileInfo(path).absolutePath());
            }
            return false;
        }
        // replaced atomically, so a crash or full disk never leaves a truncated timestamp
        if (QSaveFile file(path); !file.open(QIODevice::WriteOnly) ||
            file.write(QByteArray::number(time.toSecsSinceEpoch()) + '\n') < 0 || !file.commit()) {
            if (error) {
                *error = u"%1: %2"_s.arg(path, file.errorString());
            }
            return false;
        }
        return true;
    }

    bool within(const QString &path, const std::chrono::seconds duration, const QDateTime &now) {
        const std::optional<QDateTime> time = readTimestamp(path);
        if (!time || !now.isValid() || *time > now) {
            return false;
        }
        return time->secsTo(now) < duration.count();
    }

    std::chrono::seconds ActivityTracker::inactiveFor(const bool inUse, const QDateTime &now,
                                                      const std::optional<QDateTime> &idleSince) const {
        const std::optional<QDateTime> lastPoll = readTimestamp(m_lastPollPath);
        writeTimestamp(m_lastPollPath, now, nullptr);
        if (inUse) {
            QFile::remove(m_sincePath);
            return std::chrono::seconds(0);
        }
        // wall-clock time also passes while suspended or switched off. Only time
        // the polls saw counts, so a missed poll starts over.
        const bool watched = lastPoll && *lastPoll <= now && lastPoll->secsTo(now) <= m_maxPollGap.count();
        if (!watched) {
            writeTimestamp(m_sincePath, now, nullptr);
            return std::chrono::seconds(0);
        }
        std::optional<QDateTime> since = readTimestamp(m_sincePath);
        if (!since || *since > now) {
            // it started since the last poll. Count from when the session went
            // idle if it says, which can only be later, since that poll saw it in use.
            since = idleSince && *idleSince > *lastPoll && *idleSince <= now ? *idleSince : now;
            writeTimestamp(m_sincePath, *since, nullptr);
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
