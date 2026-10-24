// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "rpm.h"
#include "updates.h"

/// Flatpak updates, from the system and the user installation.
namespace UltramarineUpdates {

    /// A single pending update parsed from `flatpak remote-ls --updates`.
    struct FlatpakUpdate {
        QString appId;
        QString name;
        QString version;
        QString branch;
        QString origin;
        /// e.g. "app/org.kde.haruna/x86_64/stable" or "runtime/...".
        QString ref;

        bool operator==(const FlatpakUpdate &) const = default;
    };

    /// The installations queried separately, so updates are attributed to (and
    /// applied in) the right one.
    QStringList flatpakInstallations();

    /// Parses `flatpak remote-ls --updates --columns=application,name,version,branch,origin,ref`.
    QList<FlatpakUpdate> parseFlatpakRemoteLs(const QString &output);

    /// Turns a parsed update into a PackageUpdate. @p installed maps
    /// "appID/branch" to the installed version.
    PackageUpdate flatpakPackageUpdate(const FlatpakUpdate &update, const QString &installation,
                                       const QHash<QString, QString> &installed);

    /// Parses GLib's formatted sizes as printed by flatpak with the C locale:
    /// "512 bytes", "12.0 kB", "418.7 MB", "1.1 GB" (powers of 1000). 0 if unknown.
    qint64 parseSISize(QString size);

    /// Parses "application\tbranch\tsize[\tsize]" lines into "application/branch" -> sizes.
    QHash<QString, UpgradeSize> parseFlatpakSizes(const QString &output);

    /// Lists pending updates of one installation ("system" or "user").
    QList<PackageUpdate> flatpakPackageUpdates(const QString &installation, QString *error);

} // namespace UltramarineUpdates
