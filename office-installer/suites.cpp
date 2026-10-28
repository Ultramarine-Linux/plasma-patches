// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "suites.h"

#include <KLocalizedString>

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    QList<Suite> suites() {
        return {
            {
                .name = i18nc("@option:radio office suite", "Collabora Office"),
                .description = i18n("A simple, modern interface built on LibreOffice"),
                .flatpakId = u"com.collaboraoffice.Office"_s,
                .rpmPackages = {},
                .rpmExecutable = {},
                .recommended = true,
            },
            {
                .name = i18nc("@option:radio office suite", "LibreOffice"),
                .description = i18n("The full-featured LibreOffice suite"),
                .flatpakId = u"org.libreoffice.LibreOffice"_s,
                .rpmPackages = {u"libreoffice-writer"_s, u"libreoffice-calc"_s, u"libreoffice-impress"_s},
                .rpmExecutable = u"libreoffice"_s,
                .recommended = false,
            },
            {
                .name = i18nc("@option:radio office suite", "OpenOffice"),
                .description = i18n("A classic office suite, built by Apache"),
                .flatpakId = u"org.onlyoffice.desktopeditors"_s,
                .rpmPackages = {},
                .rpmExecutable = {},
                .recommended = false,
            },
        };
    }

    QList<Source> sources(const Suite &suite, const bool imageBased) {
        QList<Source> result;
        if (!suite.flatpakId.isEmpty()) {
            result.append(Source::Flatpak);
        }
        if (!suite.rpmPackages.isEmpty() && !imageBased) {
            result.append(Source::Rpm);
        }
        return result;
    }

    Command installCommand(const Suite &suite, const Source source) {
        switch (source) {
        case Source::Flatpak:
            // flatpak and PackageKit authorize through polkit, which prompts in the session.
            return {.program = u"flatpak"_s,
                    .arguments = {u"install"_s, u"--system"_s, u"--noninteractive"_s, u"-y"_s, u"flathub"_s,
                                  suite.flatpakId}};
        case Source::Rpm:
            return {u"pkcon"_s, QStringList{u"install"_s, u"--noninteractive"_s} + suite.rpmPackages};
        }
        Q_UNREACHABLE();
    }

    Command launchCommand(const Suite &suite, const Source source, const QStringList &files) {
        switch (source) {
        case Source::Flatpak:
            if (files.isEmpty()) {
                return {u"flatpak"_s, {u"run"_s, suite.flatpakId}};
            }
            // Gives the sandbox access to the files through the document portal.
            return {.program = u"flatpak"_s,
                    .arguments = QStringList{u"run"_s, u"--file-forwarding"_s, suite.flatpakId, u"@@"_s} + files +
                        QStringList{u"@@"_s}};
        case Source::Rpm:
            return {.program = suite.rpmExecutable, .arguments = files};
        }
        Q_UNREACHABLE();
    }

    bool isImageBased() { return QFileInfo::exists(u"/run/ostree-booted"_s); }

} // namespace OfficeInstaller
