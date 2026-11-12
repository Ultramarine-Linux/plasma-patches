// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "flatpak.h"

#include <algorithm>
#include <future>
#include <iterator>

#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    QStringList flatpakInstallations() { return {u"system"_s, u"user"_s}; }

    QList<FlatpakUpdate> parseFlatpakRemoteLs(const QString &output) {
        QList<FlatpakUpdate> updates;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            const QStringList fields = line.split(u'\t');
            if (fields.size() < 5 || fields[0].isEmpty() || fields[0] == "Application ID"_L1) {
                continue;
            }
            updates.append({
                .appId = fields[0],
                .name = fields[1],
                .version = fields[2],
                .branch = fields[3],
                .origin = fields[4],
                .ref = fields.value(5),
            });
        }
        return updates;
    }

    PackageUpdate flatpakPackageUpdate(const FlatpakUpdate &update, const QString &installation,
                                       const QHash<QString, QString> &installed) {
        const bool system = installation == "system"_L1;
        PackageUpdate result;
        result.name = update.name.isEmpty() ? update.appId : update.name;
        result.source = u"Flatpak"_s;
        if (!update.origin.isEmpty()) {
            result.source += u" (%1)"_s.arg(update.origin);
        }
        if (!system) {
            result.source += u" [%1]"_s.arg(installation);
        }
        // the same app ID can appear for several branches (runtime extensions)
        // and in both installations, so qualify the ID
        result.id = update.appId + "//"_L1 + update.branch;
        if (!system) {
            result.id += u'@' + installation;
        }
        const bool app = update.ref.startsWith("app/"_L1);
        result.kind = QString::fromLatin1(app ? Kind::App : Kind::Runtime);
        result.appId = app ? update.appId : QString();
        result.backend = u"flatpak"_s;
        result.oldVersion = installed.value(update.appId + u'/' + update.branch);
        result.newVersion = update.version;
        // Flatpak apps export their icon under their application ID
        result.icon = update.appId;
        return result;
    }

    qint64 parseSISize(QString size) {
        // GLib separates the number and unit with a non-breaking space
        size.replace(QChar(0x00a0), u' ');
        size = size.trimmed();
        const qsizetype space = size.indexOf(u' ');
        if (space < 0) {
            return 0;
        }
        bool ok = false;
        // QString::toDouble always uses the C locale, so "1,1" fails as it should
        const double value = size.left(space).toDouble(&ok);
        if (!ok) {
            return 0;
        }
        static const QHash<QString, double> multipliers{
            {u"byte"_s, 1}, {u"bytes"_s, 1}, {u"kB"_s, 1e3}, {u"MB"_s, 1e6}, {u"GB"_s, 1e9}, {u"TB"_s, 1e12},
        };
        const auto multiplier = multipliers.constFind(size.mid(space + 1));
        if (multiplier == multipliers.constEnd()) {
            return 0;
        }
        return static_cast<qint64>(value * *multiplier);
    }

    QHash<QString, UpgradeSize> parseFlatpakSizes(const QString &output) {
        QHash<QString, UpgradeSize> sizes;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            const QStringList fields = line.split(u'\t');
            if (fields.size() < 3 || fields[0].isEmpty()) {
                continue;
            }
            UpgradeSize size;
            size.download = parseSISize(fields[2]);
            if (fields.size() > 3) {
                size.installed = parseSISize(fields[3]);
            }
            sizes.insert(fields[0] + u'/' + fields[1], size);
        }
        return sizes;
    }

    namespace {
        /// maps "appID/branch" to the installed version for one installation
        QHash<QString, QString> installedFlatpakVersions(const QString &installation) {
            QString output;
            if (!commandOutput(u"flatpak"_s,
                               {u"list"_s, u"--"_s + installation, u"--columns=application,branch,version"_s}, &output,
                               nullptr)) {
                return {};
            }
            QHash<QString, QString> versions;
            for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
                const QStringList fields = line.split(u'\t');
                if (fields.size() < 3 || fields[0].isEmpty()) {
                    continue;
                }
                versions.insert(fields[0] + u'/' + fields[1], fields[2]);
            }
            return versions;
        }

        struct FlatpakSizes {
            QHash<QString, UpgradeSize> remote;
            /// the installed size of each ref, in UpgradeSize::download
            QHash<QString, UpgradeSize> local;
        };

        /// C locale output, so the sizes parse. A separate call from the listing, which
        /// keeps the user's locale for app names.
        FlatpakSizes flatpakSizes(const QString &installation) {
            return {
                .remote = parseFlatpakSizes(
                    localeIndependentOutput(u"flatpak"_s,
                                            {u"remote-ls"_s, u"--"_s + installation, u"--updates"_s,
                                             u"--columns=application,branch,download-size,installed-size"_s})),
                .local = parseFlatpakSizes(localeIndependentOutput(
                    u"flatpak"_s, {u"list"_s, u"--"_s + installation, u"--columns=application,branch,size"_s})),
            };
        }

        /// Flatpak reports the full download size, but with static deltas the actual download can be smaller
        void applyFlatpakSizes(QList<PackageUpdate> &updates, const QList<FlatpakUpdate> &refs,
                               const FlatpakSizes &sizes) {
            for (qsizetype i = 0; i < updates.size(); ++i) {
                const QString key = refs[i].appId + u'/' + refs[i].branch;
                const auto size = sizes.remote.constFind(key);
                if (size == sizes.remote.constEnd()) {
                    continue;
                }
                updates[i].downloadSize = size->download;
                if (size->installed > 0) {
                    updates[i].installedSizeChange = size->installed - sizes.local.value(key).download;
                }
            }
        }
    } // namespace

    QList<PackageUpdate> flatpakPackageUpdates(const QString &installation, QString *error) {
        auto sizes = std::async(std::launch::async, flatpakSizes, installation);
        QString output;
        if (!commandOutput(u"flatpak"_s,
                           {u"remote-ls"_s, u"--"_s + installation, u"--updates"_s,
                            u"--columns=application,name,version,branch,origin,ref"_s},
                           &output, error)) {
            return {};
        }
        const QList<FlatpakUpdate> updates = parseFlatpakRemoteLs(output);
        if (updates.isEmpty()) {
            return {};
        }
        const QHash<QString, QString> installed = installedFlatpakVersions(installation);
        QList<PackageUpdate> result;
        result.reserve(updates.size());
        std::ranges::transform(updates, std::back_inserter(result), [&](const FlatpakUpdate &update) {
            return flatpakPackageUpdate(update, installation, installed);
        });
        applyFlatpakSizes(result, updates, sizes.get());
        return result;
    }

} // namespace UltramarineUpdates
