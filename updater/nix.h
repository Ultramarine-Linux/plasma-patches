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

#include "updates.h"

namespace UltramarineUpdates {

    struct NixProfileElement {
        QString name;
        std::optional<bool> active;
        QString attrPath;
        QString originalUrl;
        QString url;
        std::optional<QStringList> outputs;
        QStringList storePaths;
    };

    struct NixEvalResult {
        bool error = false;
        QString name;
        QString version;
        QStringList outPaths;
    };

    std::optional<QList<NixProfileElement>> parseNixProfileList(const QByteArray &output, QString *error);

    QString storePathName(const QString &path);

    QString drvVersion(const QString &name);

    std::optional<PackageUpdate> nixUpdateFor(const NixProfileElement &element, const NixEvalResult &result);

    std::pair<QString, QString> determinateNixVersions(const QString &output);

    bool determinateNixUpgradable(const QString &installed, const QString &latest);

    QString nixBinary();
    QString determinateNixdBinary();

    bool canManageNixProfile();

    std::optional<QList<NixEvalResult>> evaluateNixUpgrades(const QList<NixProfileElement> &elements, QString *error);

    QList<PackageUpdate> nixPackageUpdates(QString *error);

    QList<PackageUpdate> determinateNixUpdate(QString *error);

    int upgradeNixProfile();

    int upgradeDeterminateNix();

} // namespace UltramarineUpdates
