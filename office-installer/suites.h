// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStringList>

#include <cstdint>

/// The office suites offered when no office suite is installed.
namespace OfficeInstaller {

    enum class Source : std::uint8_t {
        /// From Flathub, into the system installation. The default.
        Flatpak,
        /// From the distribution's repositories, through PackageKit.
        Rpm,
    };

    struct Suite {
        QString name;
        QString description;
        QString flatpakId;
        /// Empty if the suite isn't packaged in Fedora or Terra.
        QStringList rpmPackages;
        /// Opens documents after an RPM install, if the desktop database
        /// doesn't know about the suite yet.
        QString rpmExecutable;
        bool recommended = false;
    };

    struct Command {
        QString program;
        QStringList arguments;

        bool operator==(const Command &) const = default;
    };

    /// The offered suites, recommended first.
    QList<Suite> suites();

    /// Where @p suite can be installed from, the default first. RPMs are left
    /// out on image based systems (@p imageBased), where dnf can't install them.
    QList<Source> sources(const Suite &suite, bool imageBased);

    Command installCommand(const Suite &suite, Source source);

    /// Opens @p files (local paths) with a freshly installed suite.
    Command launchCommand(const Suite &suite, Source source, const QStringList &files);

    /// Whether the system was booted from an ostree deployment (bootc or rpm-ostree).
    bool isImageBased();

} // namespace OfficeInstaller
