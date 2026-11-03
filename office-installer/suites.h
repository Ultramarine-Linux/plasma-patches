// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStringList>

#include <cstdint>

/// the office suites offered when no office suite is installed
namespace OfficeInstaller {

    enum class Source : std::uint8_t {
        /// from Flathub, into the system installation (the default)
        Flatpak,
        /// from the distribution's repositories, through PackageKit
        Rpm,
    };

    struct Suite {
        QString name;
        QString description;
        QString flatpakId;
        /// empty if the suite isn't packaged in Fedora or Terra
        QStringList rpmPackages;
        /// opens documents after an RPM install, if the desktop database
        /// doesn't know about the suite yet
        QString rpmExecutable;
        bool recommended = false;
    };

    struct Command {
        QString program;
        QStringList arguments;

        bool operator==(const Command &) const = default;
    };

    /// the offered suites, recommended first
    QList<Suite> suites();

    /// where @p suite can be installed from, the default first. RPMs are left
    /// out on image based systems (@p imageBased), where dnf can't install them.
    QList<Source> sources(const Suite &suite, bool imageBased);

    /**
     * whether an installed app is @p suite: its Flatpak (desktop file names
     * start with the app ID) or its RPM (by the executable its desktop file runs)
     */
    bool isSuite(const Suite &suite, const QString &desktopEntryName, const QString &executable);

    /// the Flathub ref of @p suite's app on @p arch (Flatpak's architecture name)
    QString flatpakRef(const Suite &suite, const QString &arch);

    /// opens @p files (local paths) with a freshly installed suite
    Command launchCommand(const Suite &suite, Source source, const QStringList &files);

    /// whether the system was booted from an ostree deployment (bootc or rpm-ostree)
    bool isImageBased();

} // namespace OfficeInstaller
