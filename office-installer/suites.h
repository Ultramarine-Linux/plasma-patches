// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStringList>

#include <cstdint>

namespace OfficeInstaller {

    enum class Source : std::uint8_t {
        Flatpak,
        Rpm,
    };

    struct Suite {
        QString name;
        QString description;
        QString flatpakId;
        QStringList rpmPackages;
        QString rpmExecutable;
        bool recommended = false;
    };

    struct Command {
        QString program;
        QStringList arguments;

        bool operator==(const Command &) const = default;
    };

    QList<Suite> offeredSuites();

    QList<Source> installSources(const Suite &suite, bool imageBased);

    bool isSuite(const Suite &suite, const QString &desktopEntryName, const QString &executable);

    QString flatpakRef(const Suite &suite, const QString &flatpakArch);

    Command openFilesCommand(const Suite &suite, Source source, const QStringList &localPaths);

    bool isBootedFromOstree();

} // namespace OfficeInstaller
