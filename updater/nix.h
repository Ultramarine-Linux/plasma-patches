// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

#include "apps.h"
#include "updates.h"

/**
 * Nix support follows how Ultramarine's `nix` tweak installs Nix: the
 * Determinate Systems installer, i.e. a multi-user Determinate Nix with its
 * binaries in the default profile and determinate-nixd managing Nix itself.
 * nix-env, channels and home-manager are not handled.
 */
namespace UltramarineUpdates {

    /// one entry of `nix profile list --json`
    struct NixProfileElement {
        QString name;
        std::optional<bool> active;
        QString attrPath;
        QString originalUrl;
        QString url;
        /// null in the JSON means "the package's default outputs"
        std::optional<QStringList> outputs;
        QStringList storePaths;
    };

    /// the evaluated upgrade of one profile element
    struct NixEvalResult {
        bool error = false;
        QString name;
        QString version;
        QStringList outPaths;
    };

    /**
     * parses `nix profile list --json` into the elements that can be upgraded.
     * Version 3 (Nix 2.20+) keys elements by name, while older versions use an array.
     * Store-path installs and inactive elements are skipped. Sorted by name.
     */
    std::optional<QList<NixProfileElement>> parseNixProfileList(const QByteArray &output, QString *error);

    /// /nix/store/<hash>-flox-1.17.0 -> flox-1.17.0
    QString storePathName(const QString &path);

    /// splits off the version like builtins.parseDrvName: it starts after the
    /// first dash that isn't followed by a letter
    QString drvVersion(const QString &name);

    /// turns an element and its evaluated upgrade into an update, or nothing if
    /// the upgrade would install exactly what is installed now
    std::optional<PackageUpdate> nixUpdateFor(const NixProfileElement &element, const NixEvalResult &result);

    /// lists the app files a package installs in its store paths
    AppFiles nixAppFiles(const QStringList &storePaths);

    /// parses `determinate-nixd version` into the installed and the latest
    /// version. latest is empty if the output doesn't say (offline).
    std::pair<QString, QString> determinateNixVersions(const QString &output);

    QString nixBinary();
    QString determinateNixdBinary();

    /// whether to touch a Nix profile: nix is installed and we run as a regular
    /// user (profiles are per user)
    bool canManageNixProfile();

    /// lists pending upgrades of the user's Nix profile
    QList<PackageUpdate> nixPackageUpdates(QString *error);

    /// reports an update of Determinate Nix itself, if one is advised
    QList<PackageUpdate> determinateNixUpdate(QString *error);

    /// upgrades the user's profile. Returns the exit code.
    int upgradeNixProfile();

    /// upgrades Nix itself. Needs root. Returns the exit code.
    int upgradeDeterminateNix();

} // namespace UltramarineUpdates
