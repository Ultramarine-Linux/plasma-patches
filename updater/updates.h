// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace UltramarineUpdates {

    namespace Kind {
        inline constexpr auto App = "app";
        inline constexpr auto Addon = "addon";
        inline constexpr auto Runtime = "runtime";
        inline constexpr auto Package = "package";
    } // namespace Kind

    struct PackageUpdate {
        QString id;
        QString name;
        QString source;
        QString backend;
        QString kind;
        QString appId;
        qint64 downloadSize = 0;
        qint64 installedSizeChange = 0;
        QString oldVersion;
        QString newVersion;
        QString icon;

        bool operator==(const PackageUpdate &) const = default;

        [[nodiscard]] QJsonObject toJson() const;
    };

    struct ListResponse {
        QList<PackageUpdate> updates;
        QStringList errors;
        bool restartRequired = false;
        bool offlinePending = false;
        QString stagedVersion;
        QString offlineError;
        qsizetype queriedSources = 0;

        [[nodiscard]] bool allFailed() const { return !errors.isEmpty() && errors.size() >= queriedSources; }

        [[nodiscard]] QJsonObject toJson() const;
    };

    ListResponse listUpdates();

    struct ApplyResult {
        bool updated = false;
        int succeeded = 0;
        bool primaryFailed = false;
        QStringList failures;

        void succeed() { ++succeeded; }

        void fail(const QString &message, bool primary = false);

        [[nodiscard]] bool ok() const { return failures.isEmpty(); }

        [[nodiscard]] bool countsAsInstalled() const { return !primaryFailed && (succeeded > 0 || failures.isEmpty()); }

        [[nodiscard]] QString error() const { return failures.join(u'\n'); }
    };

    ApplyResult applyUser(bool assumeYes);

    ApplyResult applySystem(bool assumeYes, bool offline);

} // namespace UltramarineUpdates
