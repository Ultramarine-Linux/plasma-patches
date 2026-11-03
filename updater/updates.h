// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * lists and installs updates for an Ultramarine system: RPM packages (dnf) or
 * the bootc image, Flatpaks (system and user installations), Nix (Determinate
 * Nix itself and the user's `nix profile`) and Plasma widgets from the KDE Store.
 *
 * listUpdates() collects pending updates from every available source. Each
 * update says whether it's a desktop app, a runtime or another package, so the
 * Updates KCM can show apps first. A failing source is reported in
 * ListResponse::errors without hiding the others.
 *
 * applyUser() updates what belongs to the invoking user and must run as that
 * user. applySystem() updates the rest and needs root.
 */
namespace UltramarineUpdates {

    /// update kinds, so frontends can show desktop apps before libraries and runtimes
    namespace Kind {
        /// a desktop application
        inline constexpr auto App = "app";
        /// an add-on to the desktop, like a Plasma widget
        inline constexpr auto Addon = "addon";
        /// a Flatpak runtime or extension
        inline constexpr auto Runtime = "runtime";
        /// any other package (libraries, CLI tools, addons, ...)
        inline constexpr auto Package = "package";
    } // namespace Kind

    struct PackageUpdate {
        /// uniquely identifies the update: "name.arch" for RPMs, the qualified
        /// application ID for Flatpaks, "nix:<name>" for Nix, "plasmoid:<store ID>"
        /// for Plasma widgets
        QString id;
        /// the app name for desktop apps, otherwise the package or Flatpak name
        QString name;
        QString source;
        /// the package manager handling the update: "rpm", "bootc", "rpm-ostree",
        /// "flatpak", "nix" or "plasmoid"
        QString backend;
        /// one of Kind::App, Kind::Addon, Kind::Runtime or Kind::Package
        QString kind;
        /// the AppStream component ID for apps, when known
        QString appId;
        /// download size in bytes, 0 if unknown. For Flatpaks it's the full size,
        /// but static deltas can make the actual download smaller.
        qint64 downloadSize = 0;
        /// how much more (or, if negative, less) disk space the update uses once
        /// installed, in bytes. 0 if unknown.
        qint64 installedSizeChange = 0;
        QString oldVersion;
        QString newVersion;
        QString icon;

        bool operator==(const PackageUpdate &) const = default;

        [[nodiscard]] QJsonObject toJson() const;
    };

    struct ListResponse {
        QList<PackageUpdate> updates;
        /// sources that could not be queried. Updates from the others are still reported.
        QStringList errors;
        /// whether installed system updates take effect only after restarting:
        /// image mode systems with a staged image, or a pending offline update
        bool restartRequired = false;
        /// whether RPM updates are scheduled to install while restarting
        /// (`/system-update` links to PackageKit's prepared update). They're
        /// still listed as updates.
        bool offlinePending = false;
        /// the version the system runs after restarting, if known
        QString stagedVersion;
        /// why the last offline update failed while restarting, if it did
        QString offlineError;
        /// how many sources were queried, including the failed ones. Not part of the JSON.
        qsizetype queriedSources = 0;

        /// whether every queried source failed, so nothing at all is known about updates
        [[nodiscard]] bool allFailed() const { return !errors.isEmpty() && errors.size() >= queriedSources; }

        [[nodiscard]] QJsonObject toJson() const;
    };

    /// collects updates from every available source
    ListResponse listUpdates();

    /**
     * updates user-installation Flatpaks, the user's Nix profile and Plasma
     * widgets. Must run as the user: after escalating, flatpak, nix and KNewStuff
     * would only see root's (empty) installation, profile and widgets. A failing
     * one doesn't stop the others. @p error then lists every failure.
     */
    bool applyUser(bool assumeYes, QString *error);

    /**
     * updates system packages, system Flatpaks and Nix itself, and needs root. With
     * @p offline, dnf downloads the RPM updates and PackageKit prepares them to
     * install during the next restart (see packagekit.h). Sets @p updated to
     * whether any package manager was found. Like applyUser(), every package
     * manager runs even if an earlier one failed.
     */
    bool applySystem(bool assumeYes, bool offline, bool *updated, QString *error);

} // namespace UltramarineUpdates
