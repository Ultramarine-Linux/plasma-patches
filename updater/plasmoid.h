// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "apps.h"
#include "updates.h"

/**
 * Plasma widgets (plasmoids) installed from the KDE Store through "Get New
 * Widgets", the way Discover's KNewStuff backend updates them. KNewStuff keeps
 * what it installed in a per-user registry and installs into the user's home, so
 * widgets are listed and updated as the user. Widgets installed some other way
 * (packages, kpackagetool6) aren't in the registry and aren't touched.
 */
namespace UltramarineUpdates {

    /// One widget KNewStuff reports as updateable, taken out of KNSCore::Entry.
    struct PlasmoidEntry {
        /// The KDE Store content ID.
        QString uniqueId;
        QString name;
        QString version;
        QString updateVersion;
        /// For kpackages, the package directory.
        QStringList installedFiles;
    };

    /// The parts of a plasmoid's metadata.json shown in the update list.
    struct PlasmoidMetadata {
        /// The plugin ID, e.g. "org.kde.plasma.mywidget".
        QString id;
        QString icon;
    };

    /// Path of the KNewStuff registry for Plasma widgets in the user's home.
    QString plasmoidRegistryPath();

    /// Whether a KNewStuff registry records any installed (or updateable) entry.
    bool registryHasInstalledEntries(const QByteArray &registry);

    /// Parses the KPlugin section of a plasmoid's metadata.json.
    PlasmoidMetadata parsePlasmoidMetadata(const QByteArray &json);

    /// Turns a KNewStuff entry into an update, with the icon from the installed
    /// package's metadata.json (read through @p read).
    PackageUpdate plasmoidPackageUpdate(const PlasmoidEntry &entry, const FileReader &read);

    /**
     * Whether to look for widget updates: we run as a regular user and the user
     * has installed widgets from the KDE Store. Avoids network requests (and the
     * KDE Store) for everyone else.
     */
    bool canManagePlasmoids();

    /// Lists widget updates from the KDE Store.
    QList<PackageUpdate> plasmoidUpdates(QString *error);

    /// Updates every widget with a newer version in the KDE Store.
    bool upgradePlasmoids(QString *error);

} // namespace UltramarineUpdates
