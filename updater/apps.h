// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <functional>
#include <optional>

#include "updates.h"

/// Recognizes packages that are desktop apps, so they can be shown like Flatpak apps.
namespace UltramarineUpdates {

    /// What an update needs to be presented as an app.
    struct AppInfo {
        /// The AppStream component ID, if the app has metainfo.
        QString id;
        QString name;
        QString icon;

        bool operator==(const AppInfo &) const = default;
    };

    /// The subset of an AppStream metainfo file the updater uses.
    struct Metainfo {
        QString id;
        /// The untranslated name.
        QString name;
        /// A stock icon name or an absolute path, if the metainfo has one. Local
        /// metainfo usually doesn't; the launcher's icon is used instead.
        QString icon;
        /// The app's launchers ("org.kde.dolphin.desktop").
        QStringList desktopIds;

        bool operator==(const Metainfo &) const = default;
    };

    /// The fields of a .desktop file needed to present an app.
    struct DesktopEntry {
        QString name;
        QString icon;

        bool operator==(const DesktopEntry &) const = default;
    };

    /// The files of one package that describe an app.
    struct AppFiles {
        QStringList metainfo;
        QStringList desktop;
        /// Where the package's launchers live. Empty means /usr/share/applications;
        /// Nix packages use <store path>/share/applications.
        QString applicationsDir;

        bool operator==(const AppFiles &) const = default;
    };

    /// Reads a file; replaced in tests.
    using FileReader = std::function<std::optional<QByteArray>(const QString &path)>;

    /// Reads a file from disk.
    std::optional<QByteArray> readFile(const QString &path);

    /// Parses a metainfo file. Empty unless it describes a desktop application
    /// (type "desktop-application", or the legacy "desktop") with an ID and a name.
    std::optional<Metainfo> parseMetainfo(const QByteArray &content);

    /// Parses the [Desktop Entry] group of a .desktop file. Empty for entries that
    /// aren't visible applications (NoDisplay, Hidden, another Type, or no Name).
    std::optional<DesktopEntry> parseDesktopEntry(const QString &content);

    /// Parses `rpm -q --qf '[%{=NAME}.%{=ARCH}\t%{FILENAMES}\n]'` output into
    /// "name.arch" -> its metainfo files and launchers, sorted.
    QHash<QString, AppFiles> parseRpmAppFiles(const QString &output);

    /**
     * Decides whether a package is a desktop app.
     *
     * AppStream metainfo is authoritative: a package is an app if it ships a
     * desktop-application component, with the name and ID from the metainfo and
     * the icon from the metainfo or its launcher. Packages that ship metainfo but
     * no desktop application (CLI tools, addons, ...) are not apps. Only packages
     * without any metainfo, common for third-party RPMs, fall back to their
     * visible launchers.
     */
    std::optional<AppInfo> resolveApp(const QString &packageName, const AppFiles *files, const FileReader &read);

    /// Marks RPM updates that are desktop apps, with the app's name, AppStream ID and icon.
    void annotateRpmApps(QList<PackageUpdate> &updates);

    /// Applies @p app to @p update, making it an app update.
    void applyAppInfo(PackageUpdate &update, const AppInfo &app);

} // namespace UltramarineUpdates
