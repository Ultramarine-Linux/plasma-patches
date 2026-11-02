// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>

#include "updates.h"

/// RPM updates through dnf, on package-mode (non-bootc) systems
namespace UltramarineUpdates {

    /// a single pending update parsed from `dnf check-update`
    struct RpmUpdate {
        QString nameArch;
        QString version;
        QString repo;

        bool operator==(const RpmUpdate &) const = default;
    };

    /// download size and installed size of a package, in bytes
    struct UpgradeSize {
        qint64 download = 0;
        qint64 installed = 0;

        bool operator==(const UpgradeSize &) const = default;
    };

    /// parses `dnf check-update` output (dnf4 and dnf5). Only the list of upgrades
    /// is returned. The obsoletes section, headers and status lines are ignored.
    QList<RpmUpdate> parseDnfCheckUpdate(const QString &output);

    /// maps "name.arch" to the installed EVR from `rpm -qa --qf
    /// '%{NAME}.%{ARCH}\t%{EVR}\t%{INSTALLTIME}\n'`. When several versions are
    /// installed (kernels), the most recently installed wins.
    QHash<QString, QString> parseRpmInstalled(const QString &output);

    /// "python3.12-libs.x86_64" -> "python3.12-libs"
    QString rpmPackageName(const QString &nameArch);

    /// parses "key\tsize[\tsize]" lines with sizes in bytes
    QHash<QString, UpgradeSize> parseTabSizes(const QString &output);

    /// lists pending RPM updates, with sizes and app information
    QList<PackageUpdate> rpmPackageUpdates(QString *error);

} // namespace UltramarineUpdates
