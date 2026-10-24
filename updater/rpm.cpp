// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "rpm.h"

#include <QStandardPaths>

#include "apps.h"
#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    QList<RpmUpdate> parseDnfCheckUpdate(const QString &output) {
        QList<RpmUpdate> updates;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            if (line.trimmed().startsWith("Obsoleting"_L1)) {
                // Everything after this header lists obsoleted packages, not upgrades.
                break;
            }
            // Continuation lines (wrapped or obsoleted entries) are indented.
            if (line.isEmpty() || line.front() == u' ' || line.front() == u'\t') {
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

        /// Fills in sizes for RPM updates: download size and install size of the
        /// new package from dnf, minus the size of the installed one.
        void annotateRpmSizes(QList<PackageUpdate> &updates) {
            if (updates.isEmpty()) {
                return;
            }
            // dnf5's --queryformat doesn't interpret escapes, so pass real tabs/newlines.
            const QHash<QString, UpgradeSize> upgrades = parseTabSizes(
                localeIndependentOutput(u"dnf"_s,
                                        {u"repoquery"_s, u"--upgrades"_s, u"--latest-limit=1"_s, u"--queryformat"_s,
                                         u"%{name}.%{arch}\t%{downloadsize}\t%{installsize}\n"_s}));

            QStringList arguments{u"-q"_s, u"--qf"_s, u"%{NAME}.%{ARCH}\t%{SIZE}\n"_s};
            for (const PackageUpdate &update: std::as_const(updates)) {
                arguments.append(update.id);
            }
            // Install-only packages (kernels) can be installed several times; the
            // line for the last one wins, which is close enough for an estimate.
            const QHash<QString, UpgradeSize> installed = parseTabSizes(localeIndependentOutput(u"rpm"_s, arguments));

            for (PackageUpdate &update: updates) {
                const auto size = upgrades.constFind(update.id);
                if (size == upgrades.constEnd()) {
                    continue;
                }
                update.downloadSize = size->download;
                if (size->installed > 0) {
                    update.installedSizeChange = size->installed - installed.value(update.id).download;
                }
            }
        }
    } // namespace

    QList<PackageUpdate> rpmPackageUpdates(QString *error) {
        // dnf check-update exits with 100 when updates are available. No
        // --refresh: let dnf decide when metadata has expired, otherwise every
        // (unprivileged, separately cached) listing re-downloads all metadata.
        QString output;
        if (!commandOutput(QStandardPaths::findExecutable(u"dnf"_s), {u"check-update"_s}, &output, error, {100})) {
            return {};
        }
        const QList<RpmUpdate> updates = parseDnfCheckUpdate(output);
        if (updates.isEmpty()) {
            return {};
        }
        const QHash<QString, QString> installed = installedRpmVersions();
        QList<PackageUpdate> result;
        result.reserve(updates.size());
        for (const RpmUpdate &update: updates) {
            PackageUpdate packageUpdate;
            packageUpdate.id = update.nameArch;
            packageUpdate.name = rpmPackageName(update.nameArch);
            packageUpdate.source = u"RPM (%1)"_s.arg(update.repo);
            packageUpdate.backend = u"rpm"_s;
            packageUpdate.kind = QString::fromLatin1(Kind::Package);
            packageUpdate.oldVersion = installed.value(update.nameArch);
            packageUpdate.newVersion = update.version;
            packageUpdate.icon = u"package-x-generic"_s;
            result.append(packageUpdate);
        }
        annotateRpmApps(result);
        annotateRpmSizes(result);
        return result;
    }

} // namespace UltramarineUpdates
