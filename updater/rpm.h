// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>

#include "updates.h"

namespace UltramarineUpdates {

    struct RpmUpdate {
        QString nameArch;
        QString version;
        QString repo;

        bool operator==(const RpmUpdate &) const = default;
    };

    struct UpgradeSize {
        qint64 download = 0;
        qint64 installed = 0;

        bool operator==(const UpgradeSize &) const = default;
    };

    struct UpgradeSizes {
        QHash<QString, UpgradeSize> upgrades;
        QHash<QString, UpgradeSize> current;

        void annotate(PackageUpdate &update, const QString &key) const;
    };

    QList<RpmUpdate> parseDnfCheckUpdate(const QString &output);

    QHash<QString, QString> parseRpmInstalled(const QString &output);

    QString rpmPackageName(const QString &nameArch);

    QHash<QString, UpgradeSize> parseTabSizes(const QString &output);

    QList<PackageUpdate> rpmPackageUpdates(QString *error);

} // namespace UltramarineUpdates
