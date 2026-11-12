// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace UltramarineUpdates {

    struct PackageKitPaths {
        QString preparedUpdate = QStringLiteral("/var/lib/PackageKit/prepared-update");
        QString preparedUpgrade = QStringLiteral("/var/lib/PackageKit/prepared-upgrade");
        QString trigger = QStringLiteral("/system-update");
        // sic, PackageKit's spelling
        QString results = QStringLiteral("/var/lib/PackageKit/offline-update-competed");
        QString osRelease = QStringLiteral("/etc/os-release");
        QString rpmDatabase = QStringLiteral("/usr/lib/sysimage/rpm/rpmdb.sqlite");
    };

    enum class SystemUpdateTrigger : std::uint8_t {
        None,
        PackageKitUpdate,
        PackageKitUpgrade,
        Other,
    };

    SystemUpdateTrigger classifySystemUpdateTarget(const QString &target, const PackageKitPaths &paths = {});

    SystemUpdateTrigger systemUpdateTrigger(const PackageKitPaths &paths = {});

    bool offlineUpdatePending(const PackageKitPaths &paths = {});

    QString parseOfflineResultsError(const QByteArray &keyFile);

    QString lastOfflineUpdateError(const PackageKitPaths &paths = {});

    QString parseOsReleaseVersionId(const QByteArray &osRelease);

    QString packageKitCacheDir(const PackageKitPaths &paths = {});

    QString packageKitClient();

    QString pkgcliError(const QByteArray &output);

    bool prepareOfflineUpdate(QString *error);

} // namespace UltramarineUpdates
