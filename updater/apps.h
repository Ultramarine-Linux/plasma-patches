// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <functional>
#include <optional>

#include "updates.h"

namespace UltramarineUpdates {

    struct AppInfo {
        QString id;
        QString name;
        QString icon;

        bool operator==(const AppInfo &) const = default;
    };

    struct Metainfo {
        QString id;
        QString name;
        QString icon;
        QStringList desktopIds;

        bool operator==(const Metainfo &) const = default;
    };

    struct DesktopEntry {
        QString name;
        QString icon;

        bool operator==(const DesktopEntry &) const = default;
    };

    struct AppFiles {
        QStringList metainfo;
        QStringList desktop;
        QString applicationsDir;

        bool operator==(const AppFiles &) const = default;
    };

    using FileReader = std::function<std::optional<QByteArray>(const QString &path)>;

    std::optional<QByteArray> readFile(const QString &path);

    std::optional<Metainfo> parseMetainfo(const QByteArray &content);

    std::optional<DesktopEntry> parseDesktopEntry(const QString &content);

    QHash<QString, AppFiles> parseRpmAppFiles(const QString &output);

    std::optional<AppInfo> resolveApp(const QString &packageName, const AppFiles *files, const FileReader &read);

    void annotateRpmApps(QList<PackageUpdate> &updates);

    void applyAppInfo(PackageUpdate &update, const AppInfo &app);

} // namespace UltramarineUpdates
