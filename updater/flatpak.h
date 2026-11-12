// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "rpm.h"
#include "updates.h"

namespace UltramarineUpdates {

    struct FlatpakUpdate {
        QString appId;
        QString name;
        QString version;
        QString branch;
        QString origin;
        QString ref;

        bool operator==(const FlatpakUpdate &) const = default;
    };

    QList<FlatpakUpdate> parseFlatpakRemoteLs(const QString &output);

    PackageUpdate flatpakPackageUpdate(const FlatpakUpdate &update, const QString &installation,
                                       const QHash<QString, QString> &installed);

    qint64 parseSISize(QString size);

    QHash<QString, UpgradeSize> parseFlatpakSizes(const QString &output);

    QList<PackageUpdate> flatpakPackageUpdates(const QString &installation, QString *error);

} // namespace UltramarineUpdates
