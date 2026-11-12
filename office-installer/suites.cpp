// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "suites.h"

#include <KLocalizedString>

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    QList<Suite> offeredSuites() {
        return {
            {
                .name = i18nc("@option:radio office suite", "Collabora Office"),
                .description = i18n("A simple, modern interface built on LibreOffice"),
                .flatpakId = u"com.collaboraoffice.Office"_s,
                .rpmPackages = {u"collabora-office"_s},
                .rpmExecutable = u"coda-qt"_s,
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
                .name = i18nc("@option:radio office suite", "ONLYOFFICE"),
                .description = i18n("Focused on compatibility with Microsoft Office documents"),
                .flatpakId = u"org.onlyoffice.desktopeditors"_s,
                .rpmPackages = {},
                .rpmExecutable = {},
                .recommended = false,
            },
        };
    }

    QList<Source> installSources(const Suite &suite, const bool imageBased) {
        QList<Source> result;
        if (!suite.flatpakId.isEmpty()) {
            result.append(Source::Flatpak);
        }
        if (!suite.rpmPackages.isEmpty() && !imageBased) {
            result.append(Source::Rpm);
        }
        return result;
    }

    namespace {
        bool isDesktopFileOfFlatpak(const QString &desktopEntryName, const QString &flatpakId) {
            const bool isMainDesktopFile = desktopEntryName.compare(flatpakId, Qt::CaseInsensitive) == 0;
            const bool isSubAppDesktopFile = desktopEntryName.startsWith(flatpakId + u'.', Qt::CaseInsensitive);
            return isMainDesktopFile || isSubAppDesktopFile;
        }
    } // namespace

    bool isSuite(const Suite &suite, const QString &desktopEntryName, const QString &executable) {
        if (!suite.flatpakId.isEmpty() && isDesktopFileOfFlatpak(desktopEntryName, suite.flatpakId)) {
            return true;
        }
        return !suite.rpmExecutable.isEmpty() && executable == suite.rpmExecutable;
    }

    QString flatpakRef(const Suite &suite, const QString &flatpakArch) {
        return u"app/%1/%2/stable"_s.arg(suite.flatpakId, flatpakArch);
    }

    Command openFilesCommand(const Suite &suite, const Source source, const QStringList &localPaths) {
        switch (source) {
        case Source::Flatpak:
            if (localPaths.isEmpty()) {
                return {u"flatpak"_s, {u"run"_s, suite.flatpakId}};
            }
            return {.program = u"flatpak"_s,
                    .arguments = QStringList{u"run"_s, u"--file-forwarding"_s, suite.flatpakId, u"@@"_s} + localPaths +
                        QStringList{u"@@"_s}};
        case Source::Rpm:
            return {.program = suite.rpmExecutable, .arguments = localPaths};
        }
        Q_UNREACHABLE();
    }

    bool isBootedFromOstree() { return QFileInfo::exists(u"/run/ostree-booted"_s); }

} // namespace OfficeInstaller
