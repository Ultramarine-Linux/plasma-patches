// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * Lists and installs updates for an Ultramarine system: RPM packages (dnf),
 * Flatpaks (system and user installations) and Nix (Determinate Nix itself and
 * the user's `nix profile`).
 *
 * listUpdates() collects pending updates from every available source. Each
 * update says whether it's a desktop app, a runtime or another package, so the
 * Updates KCM can show apps first. A failing source is reported in
 * ListResponse::errors without hiding the others.
 *
 * applyUser() updates what belongs to the invoking user and must run as that
 * user; applySystem() updates the rest and needs root.
 */
namespace UltramarineUpdates {

    /// Update kinds, so frontends can show desktop apps before libraries and runtimes.
    namespace Kind {
        /// A desktop application.
        inline constexpr auto App = "app";
        /// A Flatpak runtime or extension.
        inline constexpr auto Runtime = "runtime";
        /// Any other package (libraries, CLI tools, addons, ...).
        inline constexpr auto Package = "package";
    } // namespace Kind

    struct PackageUpdate {
        /// Uniquely identifies the update: "name.arch" for RPMs, the qualified
        /// application ID for Flatpaks, "nix:<name>" for Nix.
        QString id;
        /// The app name for desktop apps, otherwise the package or Flatpak name.
        QString name;
        QString source;
        /// The package manager handling the update: "rpm", "flatpak" or "nix".
        QString backend;
        /// One of Kind::App, Kind::Runtime or Kind::Package.
        QString kind;
        /// The AppStream component ID for apps, when known.
        QString appId;
        /// Download size in bytes, 0 if unknown. For Flatpaks it's the full size;
        /// static deltas can make the actual download smaller.
        qint64 downloadSize = 0;
        /// How much more (or, if negative, less) disk space the update uses once
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
        /// Sources that could not be queried. Updates from the others are still reported.
        QStringList errors;

        [[nodiscard]] QJsonObject toJson() const;
    };

    /// Collects updates from every available source.
    ListResponse listUpdates();

    /**
     * Updates user-installation Flatpaks and the user's Nix profile. Must run as
     * the user: after escalating, flatpak and nix would only see root's (empty)
     * installation and profile.
     */
    bool applyUser(bool assumeYes, QString *error);

    /**
     * Updates system packages, system Flatpaks and Nix itself. Needs root. With
     * @p offline, RPM updates are staged for the next restart (dnf5 offline
     * transactions). Sets @p updated to whether any package manager was found.
     */
    bool applySystem(bool assumeYes, bool offline, bool *updated, QString *error);

} // namespace UltramarineUpdates
