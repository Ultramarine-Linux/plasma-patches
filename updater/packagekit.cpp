// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "packagekit.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <optional>

#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        /// the group pk_offline_auth_set_results() writes
        constexpr auto resultsGroup = "[PackageKit Offline Update Results]";

        /// undoes GKeyFile's escaping of string values (g_key_file_parse_value_as_string())
        QString unescapeKeyFileValue(const QByteArray &value) {
            QByteArray result;
            result.reserve(value.size());
            for (qsizetype i = 0; i < value.size(); ++i) {
                if (value.at(i) != '\\' || i + 1 == value.size()) {
                    result += value.at(i);
                    continue;
                }
                switch (const char escaped = value.at(++i)) {
                case 's':
                    result += ' ';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                default:
                    result += escaped;
                }
            }
            return QString::fromUtf8(result);
        }

        /// like dnf5_releasever_is_valid(): never empty, an option, a hidden directory or a path
        bool isSafePathComponent(const QString &value) {
            if (value.isEmpty() || !value.front().isLetterOrNumber() || value.contains(".."_L1)) {
                return false;
            }
            return std::ranges::all_of(value, [](const QChar c) {
                return (c.isLetterOrNumber() && c.unicode() < 128) || c == u'.' || c == u'_' || c == u'+' || c == u'-';
            });
        }

        /// the value of an os-release line, unquoted like g_shell_unquote()
        QString unquoteShellValue(const QString &value) {
            if (value.size() >= 2 && value.front() == u'\'' && value.back() == u'\'') {
                return value.mid(1, value.size() - 2);
            }
            if (value.size() >= 2 && value.front() == u'"' && value.back() == u'"') {
                QString result;
                for (qsizetype i = 1; i < value.size() - 1; ++i) {
                    if (value.at(i) == u'\\' && i + 2 < value.size()) {
                        ++i;
                    }
                    result += value.at(i);
                }
                return result;
            }
            return value;
        }
    } // namespace

    SystemUpdateTrigger classifySystemUpdateTarget(const QString &target, const PackageKitPaths &paths) {
        if (target.isEmpty()) {
            return SystemUpdateTrigger::None;
        }
        if (target == paths.preparedUpdate) {
            return SystemUpdateTrigger::PackageKitUpdate;
        }
        if (target == paths.preparedUpgrade) {
            return SystemUpdateTrigger::PackageKitUpgrade;
        }
        return SystemUpdateTrigger::Other;
    }

    SystemUpdateTrigger systemUpdateTrigger(const PackageKitPaths &paths) {
        const QFileInfo trigger(paths.trigger);
        if (trigger.isSymLink()) {
            return classifySystemUpdateTarget(trigger.readSymLink(), paths);
        }
        return trigger.exists() ? SystemUpdateTrigger::Other : SystemUpdateTrigger::None;
    }

    bool offlineUpdatePending(const PackageKitPaths &paths) {
        return systemUpdateTrigger(paths) == SystemUpdateTrigger::PackageKitUpdate &&
            QFileInfo::exists(paths.preparedUpdate);
    }

    QString parseOfflineResultsError(const QByteArray &keyFile) {
        // carries over between lines, so it can't move into the loop
        // cppcheck-suppress variableScope
        bool inGroup = false;
        std::optional<bool> success;
        QString details;
        QString code;
        for (const QList<QByteArray> lines = keyFile.split('\n'); const QByteArray &rawLine: lines) {
            const QByteArray line = rawLine.trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue;
            }
            if (line.startsWith('[')) {
                inGroup = line == resultsGroup;
                continue;
            }
            const qsizetype equals = line.indexOf('=');
            if (!inGroup || equals < 0) {
                continue;
            }
            const QByteArray key = line.left(equals).trimmed();
            const QByteArray value = line.mid(equals + 1).trimmed();
            if (key == "Success") {
                success = value == "true";
            } else if (key == "ErrorDetails") {
                details = unescapeKeyFileValue(value).trimmed();
            } else if (key == "ErrorCode") {
                code = unescapeKeyFileValue(value).trimmed();
            }
        }
        if (success.value_or(true)) {
            return {};
        }
        if (!details.isEmpty()) {
            return details;
        }
        return code.isEmpty() ? u"unknown error"_s : code;
    }

    QString lastOfflineUpdateError(const PackageKitPaths &paths) {
        QFile results(paths.results);
        if (!results.open(QIODevice::ReadOnly)) {
            return {};
        }
        QString error = parseOfflineResultsError(results.read(qint64{64} * 1024));
        // packages were installed (or removed) since, by PackageKit or anything
        // else, so the failure is probably no longer relevant
        if (const QFileInfo rpmDatabase(paths.rpmDatabase); !error.isEmpty() && rpmDatabase.exists() &&
            rpmDatabase.lastModified() > results.fileTime(QFileDevice::FileModificationTime)) {
            return {};
        }
        return error;
    }

    QString parseOsReleaseVersionId(const QByteArray &osRelease) {
        QString versionId;
        for (const QList<QByteArray> lines = osRelease.split('\n'); const QByteArray &rawLine: lines) {
            const QString line = QString::fromUtf8(rawLine).trimmed();
            if (const qsizetype equals = line.indexOf(u'=');
                equals > 0 && QStringView(line).left(equals).trimmed() == "VERSION_ID"_L1) {
                versionId = unquoteShellValue(line.mid(equals + 1).trimmed());
            }
        }
        return isSafePathComponent(versionId) ? versionId : QString();
    }

    QString packageKitCacheDir(const PackageKitPaths &paths) {
        // PackageKit falls back to /usr/lib/os-release, like systemd
        QFile osRelease(paths.osRelease);
        if (!osRelease.exists()) {
            osRelease.setFileName(u"/usr/lib/os-release"_s);
        }
        if (!osRelease.open(QIODevice::ReadOnly)) {
            return {};
        }
        const QString versionId = parseOsReleaseVersionId(osRelease.read(qint64{64} * 1024));
        return versionId.isEmpty() ? QString() : u"/var/cache/PackageKit/%1/metadata"_s.arg(versionId);
    }

    QString packageKitClient() { return findExecutable(u"pkgcli"_s); }

    QString pkgcliError(const QByteArray &output) {
        const QString line = lastLine(QString::fromUtf8(output));
        return line.startsWith("Error: "_L1) ? line.mid(7) : line;
    }

    bool prepareOfflineUpdate(QString *error) {
        const QString pkgcli = packageKitClient();
        if (pkgcli.isEmpty()) {
            *error = u"Installing updates while restarting needs PackageKit, which isn't installed"_s;
            return false;
        }
        const auto run = [&pkgcli, error](const QStringList &arguments) {
            const CommandResult result = capture(pkgcli, arguments);
            if (result.succeeded()) {
                return true;
            }
            QString message = pkgcliError(result.err.trimmed().isEmpty() ? result.out : result.err);
            if (!result.started) {
                message = u"pkgcli could not be started"_s;
            } else if (message.isEmpty()) {
                message = u"pkgcli failed (exit code %1)"_s.arg(result.exitCode);
            }
            *error = u"PackageKit could not prepare the update: %1"_s.arg(message);
            return false;
        };

        std::fputs("Preparing the updates to install while restarting...\n", stdout);
        std::fflush(stdout);
        // a running packagekitd keeps the repository metadata it loaded, so a
        // (not forced) refresh rereads it from the cache dnf just updated,
        // without downloading anything. Then PackageKit finds the packages dnf
        // downloaded, records them and links /system-update to them, and it doesn't
        // trigger anything if it finds no updates.
        return run({u"--no-color"_s, u"refresh"_s}) &&
            run({u"--yes"_s, u"--no-color"_s, u"offline-update"_s, u"prepare"_s});
    }

} // namespace UltramarineUpdates
