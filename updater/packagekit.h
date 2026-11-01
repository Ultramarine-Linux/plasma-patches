// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QString>

/**
 * PackageKit offline updates: RPM updates installed while restarting
 *
 * dnf downloads the updates into PackageKit's cache (so its error messages are
 * the ones shown), then PackageKit prepares them from there without
 * downloading again (`pkgcli offline-update prepare`: it records the packages in
 * /var/lib/PackageKit/prepared-update and links `/system-update` to it), and
 * packagekit-offline-update.service installs them during the next restart
 *
 * going through PackageKit is what makes Plasma's logout screen offer "Install
 * Updates and Restart" next to a plain Restart, which cancels them through
 * PackageKit's Offline D-Bus interface
 */
namespace UltramarineUpdates {

    /// PackageKit's offline update files (lib/pk-offline.c) and the rpm database
    struct PackageKitPaths {
        QString preparedUpdate = QStringLiteral("/var/lib/PackageKit/prepared-update");
        QString preparedUpgrade = QStringLiteral("/var/lib/PackageKit/prepared-upgrade");
        /// systemd's trigger for system-update.target
        QString trigger = QStringLiteral("/system-update");
        /// the outcome of the last offline update (PackageKit's spelling)
        QString results = QStringLiteral("/var/lib/PackageKit/offline-update-competed");
        QString osRelease = QStringLiteral("/etc/os-release");
        QString rpmDatabase = QStringLiteral("/usr/lib/sysimage/rpm/rpmdb.sqlite");
    };

    /// what `/system-update` starts during the next restart
    enum class SystemUpdateTrigger {
        None,
        /// PackageKit's prepared update
        PackageKitUpdate,
        /// PackageKit's prepared system upgrade (like from Discover)
        PackageKitUpgrade,
        /// another tool's offline update, like `dnf offline`
        Other,
    };

    /// classifies `/system-update` by where it links to (empty: it doesn't exist)
    SystemUpdateTrigger classifySystemUpdateTarget(const QString &target, const PackageKitPaths &paths = {});

    SystemUpdateTrigger systemUpdateTrigger(const PackageKitPaths &paths = {});

    /**
     * whether PackageKit installs prepared RPM updates during the next restart:
     * `/system-update` links to the prepared update, which exists; the same
     * check as Plasma's logout screen
     */
    bool offlineUpdatePending(const PackageKitPaths &paths = {});

    /**
     * parses PackageKit's offline update results (a GKeyFile, see
     * pk_offline_auth_set_results()); returns the error if the update failed,
     * otherwise (or if it can't be parsed) an empty string
     */
    QString parseOfflineResultsError(const QByteArray &keyFile);

    /**
     * the error of the last offline update, if it failed and nothing was
     * installed since (the rpm database is older than the results)
     */
    QString lastOfflineUpdateError(const PackageKitPaths &paths = {});

    /**
     * VERSION_ID from os-release, as PackageKit reads it (pk_parse_os_release());
     * empty if it's missing or couldn't safely be part of a path
     */
    QString parseOsReleaseVersionId(const QByteArray &osRelease);

    /**
     * where PackageKit's dnf5 backend keeps repository metadata and packages
     * (dnf5_setup_base(): /var/cache/PackageKit/<VERSION_ID>/metadata); empty if
     * VERSION_ID is missing (PackageKit then uses dnf's default cache too) or
     * not safe in a path
     */
    QString packageKitCacheDir(const PackageKitPaths &paths = {});

    /// pkgcli, PackageKit's command line client, or empty if PackageKit isn't installed
    QString packageKitClient();

    /// the message in pkgcli's output: the last line, without its "Error: " prefix
    QString pkgcliError(const QByteArray &output);

    /**
     * prepares the updates dnf downloaded to PackageKit's cache and triggers
     * them for the next restart; nothing is triggered if PackageKit finds no
     * updates, needs root
     */
    bool prepareOfflineUpdate(QString *error);

} // namespace UltramarineUpdates
