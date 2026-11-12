// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "rpm.h"

#include <QStandardPaths>

#include <algorithm>
#include <future>
#include <iterator>

#include "apps.h"
#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    QList<RpmUpdate> parseDnfCheckUpdate(const QString &output) {
        QList<RpmUpdate> updates;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            if (const bool obsoletesSectionStarts = line.trimmed().startsWith("Obsoleting"_L1);
                obsoletesSectionStarts) {
                break;
            }
            if (const bool isContinuationLine = !line.isEmpty() && (line.front() == u' ' || line.front() == u'\t');
                line.isEmpty() || isContinuationLine) {
                continue;
            }
            const QStringList fields = line.simplified().split(u' ', Qt::SkipEmptyParts);
            if (fields.size() != 3 || !fields[0].contains(u'.') || fields[0].endsWith(u':')) {
                continue;
            }
            updates.append({.nameArch = fields[0], .version = fields[1], .repo = fields[2]});
        }
        return updates;
    }

    QHash<QString, QString> parseRpmInstalled(const QString &output) {
        QHash<QString, QString> versions;
        QHash<QString, qint64> installTimes;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            const QStringList fields = line.split(u'\t');
            if (fields.size() != 3) {
                continue;
            }
            const qint64 installTime = fields[2].toLongLong();
            if (const auto previous = installTimes.constFind(fields[0]);
                previous != installTimes.constEnd() && *previous > installTime) {
                continue;
            }
            versions.insert(fields[0], fields[1]);
            installTimes.insert(fields[0], installTime);
        }
        return versions;
    }

    QString rpmPackageName(const QString &nameArch) {
        if (const qsizetype dot = nameArch.lastIndexOf(u'.'); dot > 0) {
            return nameArch.left(dot);
        }
        return nameArch;
    }

    QHash<QString, UpgradeSize> parseTabSizes(const QString &output) {
        QHash<QString, UpgradeSize> sizes;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            const QStringList fields = line.trimmed().split(u'\t');
            if (fields.size() < 2 || fields[0].isEmpty()) {
                continue;
            }
            UpgradeSize size;
            size.download = fields[1].toLongLong();
            if (fields.size() > 2) {
                size.installed = fields[2].toLongLong();
            }
            sizes.insert(fields[0], size);
        }
        return sizes;
    }

    namespace {
        QHash<QString, QString> installedRpmVersions() {
            QString output;
            if (!commandOutput(u"rpm"_s, {u"-qa"_s, u"--qf"_s, u"%{NAME}.%{ARCH}\t%{EVR}\t%{INSTALLTIME}\n"_s}, &output,
                               nullptr)) {
                return {};
            }
            return parseRpmInstalled(output);
        }

        struct RpmSizes {
            QHash<QString, UpgradeSize> upgrades;
            QHash<QString, UpgradeSize> installedSizeColumns;

            [[nodiscard]] qint64 installedSize(const QString &nameArch) const {
                return installedSizeColumns.value(nameArch).download;
            }
        };

        RpmSizes rpmSizes(const QStringList &nameArchs) {
            RpmSizes sizes;
            sizes.upgrades = parseTabSizes(
                localeIndependentOutput(u"dnf"_s,
                                        {u"repoquery"_s, u"--upgrades"_s, u"--latest-limit=1"_s, u"--queryformat"_s,
                                         u"%{name}.%{arch}\t%{downloadsize}\t%{installsize}\n"_s}));
            sizes.installedSizeColumns = parseTabSizes(localeIndependentOutput(
                u"rpm"_s, QStringList{u"-q"_s, u"--qf"_s, u"%{NAME}.%{ARCH}\t%{SIZE}\n"_s} + nameArchs));
            return sizes;
        }

        void applyRpmSizes(QList<PackageUpdate> &updates, const RpmSizes &sizes) {
            for (PackageUpdate &update: updates) {
                const auto size = sizes.upgrades.constFind(update.id);
                if (size == sizes.upgrades.constEnd()) {
                    continue;
                }
                update.downloadSize = size->download;
                if (size->installed > 0) {
                    update.installedSizeChange = size->installed - sizes.installedSize(update.id);
                }
            }
        }
    } // namespace

    QList<PackageUpdate> rpmPackageUpdates(QString *error) {
        constexpr int dnfUpdatesAvailableExitCode = 100;
        QString output;
        // no --refresh, or every unprivileged listing re-downloads all metadata
        if (!commandOutput(QStandardPaths::findExecutable(u"dnf"_s), {u"check-update"_s}, &output, error,
                           {dnfUpdatesAvailableExitCode})) {
            return {};
        }
        const QList<RpmUpdate> updates = parseDnfCheckUpdate(output);
        if (updates.isEmpty()) {
            return {};
        }
        QStringList nameArchs;
        nameArchs.reserve(updates.size());
        std::ranges::transform(updates, std::back_inserter(nameArchs), &RpmUpdate::nameArch);
        auto installed = std::async(std::launch::async, installedRpmVersions);
        auto sizes = std::async(std::launch::async, rpmSizes, nameArchs);

        QList<PackageUpdate> result;
        result.reserve(updates.size());
        for (const auto &[nameArch, version, repo]: updates) {
            PackageUpdate packageUpdate;
            packageUpdate.id = nameArch;
            packageUpdate.name = rpmPackageName(nameArch);
            packageUpdate.source = u"RPM (%1)"_s.arg(repo);
            packageUpdate.backend = u"rpm"_s;
            packageUpdate.kind = QString::fromLatin1(Kind::Package);
            packageUpdate.newVersion = version;
            packageUpdate.icon = u"package-x-generic"_s;
            result.append(packageUpdate);
        }
        annotateRpmApps(result);

        const QHash<QString, QString> installedVersions = installed.get();
        for (PackageUpdate &update: result) {
            update.oldVersion = installedVersions.value(update.id);
        }
        applyRpmSizes(result, sizes.get());
        return result;
    }

} // namespace UltramarineUpdates
