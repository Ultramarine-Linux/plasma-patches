// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "updates.h"

#include <QJsonArray>
#include <QStandardPaths>

#include "flatpak.h"
#include "image.h"
#include "nix.h"
#include "plasmoid.h"
#include "process.h"
#include "rpm.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    QJsonObject PackageUpdate::toJson() const {
        QJsonObject object{
            {u"id"_s, id},
            {u"name"_s, name},
            {u"source"_s, source},
            {u"backend"_s, backend},
            {u"kind"_s, kind},
            {u"download_size"_s, downloadSize},
            {u"installed_size_change"_s, installedSizeChange},
            {u"old_version"_s, oldVersion},
            {u"new_version"_s, newVersion},
            {u"icon"_s, icon},
        };
        if (!appId.isEmpty()) {
            object.insert(u"app_id"_s, appId);
        }
        return object;
    }

    QJsonObject ListResponse::toJson() const {
        QJsonArray updateArray;
        for (const PackageUpdate &update: updates) {
            updateArray.append(update.toJson());
        }
        QJsonObject object{
            {u"updates"_s, updateArray},
            {u"errors"_s, QJsonArray::fromStringList(errors)},
            {u"restart_required"_s, restartRequired},
        };
        if (!stagedVersion.isEmpty()) {
            object.insert(u"staged_version"_s, stagedVersion);
        }
        return object;
    }

    namespace {
        bool hasExecutable(const QString &name) { return !QStandardPaths::findExecutable(name).isEmpty(); }
    } // namespace

    ListResponse listUpdates() {
        ListResponse response;
        const auto collect = [&response](const QString &source, const QList<PackageUpdate> &updates,
                                         const QString &error) {
            if (!error.isEmpty()) {
                response.errors.append(u"%1: %2"_s.arg(source, error));
                return;
            }
            response.updates.append(updates);
        };

        QString error;
        if (isImageMode()) {
            // dnf exists in bootc images, but can't update the image.
            const ImageState image = imageState(&error);
            collect(u"image"_s, image.update ? QList<PackageUpdate>{*image.update} : QList<PackageUpdate>{}, error);
            response.restartRequired = image.staged;
            response.stagedVersion = image.stagedVersion;
        } else if (hasExecutable(u"dnf"_s)) {
            const QList<PackageUpdate> updates = rpmPackageUpdates(&error);
            collect(u"rpm"_s, updates, error);
        }
        if (!determinateNixdBinary().isEmpty()) {
            error.clear();
            const QList<PackageUpdate> updates = determinateNixUpdate(&error);
            collect(u"determinate nix"_s, updates, error);
        }
        if (canManageNixProfile()) {
            error.clear();
            const QList<PackageUpdate> updates = nixPackageUpdates(&error);
            collect(u"nix profile"_s, updates, error);
        }
        if (hasExecutable(u"flatpak"_s)) {
            for (const QString &installation: flatpakInstallations()) {
                error.clear();
                const QList<PackageUpdate> updates = flatpakPackageUpdates(installation, &error);
                collect(u"flatpak "_s + installation, updates, error);
            }
        }
        if (canManagePlasmoids()) {
            error.clear();
            const QList<PackageUpdate> updates = plasmoidUpdates(&error);
            collect(u"plasma widgets"_s, updates, error);
        }
        return response;
    }

    bool applyUser(const bool assumeYes, QString *error) {
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            QStringList arguments{u"update"_s, u"--user"_s};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            if (const int exitCode = runForwarded(flatpak, arguments); exitCode != 0) {
                *error = u"Flatpak user update failed (exit code %1)"_s.arg(exitCode);
                return false;
            }
        }
        // `nix profile upgrade` doesn't prompt.
        if (canManageNixProfile()) {
            if (const int exitCode = upgradeNixProfile(); exitCode != 0) {
                *error = u"Nix profile upgrade failed (exit code %1)"_s.arg(exitCode);
                return false;
            }
        }
        if (canManagePlasmoids()) {
            if (QString plasmoidError; !upgradePlasmoids(&plasmoidError)) {
                *error = u"Plasma widget update failed: %1"_s.arg(plasmoidError);
                return false;
            }
        }
        return true;
    }

    bool applySystem(const bool assumeYes, const bool offline, bool *updated, QString *error) {
        *updated = false;
        if (isImageMode()) {
            // The new image is always staged for the next boot, so offline doesn't apply.
            if (!upgradeImage(error)) {
                return false;
            }
            *updated = true;
        } else if (const QString dnf = QStandardPaths::findExecutable(u"dnf"_s); !dnf.isEmpty()) {
            QStringList arguments{u"upgrade"_s, u"--refresh"_s};
            if (offline) {
                arguments.append(u"--offline"_s);
            }
            if (assumeYes) {
                arguments.append(u"-y"_s);
            }
            if (const int exitCode = runForwarded(dnf, arguments); exitCode != 0) {
                *error = u"RPM update failed (exit code %1)"_s.arg(exitCode);
                return false;
            }
            *updated = true;
        }
        if (!determinateNixdBinary().isEmpty()) {
            // Nix itself, installed system-wide by the nix tweak. Only upgrades
            // when Determinate Systems advises a newer version.
            if (const int exitCode = upgradeDeterminateNix(); exitCode != 0) {
                *error = u"Determinate Nix upgrade failed (exit code %1)"_s.arg(exitCode);
                return false;
            }
            *updated = true;
        }
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            QStringList arguments{u"update"_s, u"--system"_s};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            if (const int exitCode = runForwarded(flatpak, arguments); exitCode != 0) {
                *error = u"Flatpak update failed (exit code %1)"_s.arg(exitCode);
                return false;
            }
            *updated = true;
        }
        return true;
    }

} // namespace UltramarineUpdates
