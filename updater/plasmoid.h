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

namespace UltramarineUpdates {

    struct PlasmoidEntry {
        QString uniqueId;
        QString name;
        QString version;
        QString updateVersion;
        QStringList installedFiles;
    };

    struct PlasmoidMetadata {
        QString id;
        QString icon;
    };

    QString plasmoidRegistryPath();

    bool registryHasInstalledEntries(const QByteArray &registry);

    PlasmoidMetadata parsePlasmoidMetadata(const QByteArray &json);

    PackageUpdate plasmoidPackageUpdate(const PlasmoidEntry &entry, const FileReader &read);

    bool canManagePlasmoids();

    QList<PackageUpdate> plasmoidUpdates(QString *error);

    bool upgradePlasmoids(QString *error);

} // namespace UltramarineUpdates
