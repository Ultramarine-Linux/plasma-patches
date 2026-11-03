// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "updates.h"

#include <QJsonArray>
#include <QStandardPaths>

#include "flatpak.h"
#include "image.h"
#include "nix.h"
#include "packagekit.h"
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
            {u"offline_pending"_s, offlinePending},
        };
        if (!stagedVersion.isEmpty()) {
            object.insert(u"staged_version"_s, stagedVersion);
        }
        if (!offlineError.isEmpty()) {
            object.insert(u"offline_error"_s, offlineError);
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
            ++response.queriedSources;
            if (!error.isEmpty()) {
                response.errors.append(u"%1: %2"_s.arg(source, error));
                return;
            }
            response.updates.append(updates);
        };

        QString error;
        if (isImageMode()) {
            // dnf exists in bootc images, but can't update the image
            const ImageState image = imageState(&error);
            collect(u"image"_s, image.update ? QList<PackageUpdate>{*image.update} : QList<PackageUpdate>{}, error);
            response.restartRequired = image.staged;
            response.stagedVersion = image.stagedVersion;
        } else if (hasExecutable(u"dnf"_s)) {
            const QList<PackageUpdate> updates = rpmPackageUpdates(&error);
            collect(u"rpm"_s, updates, error);
            response.offlinePending = offlineUpdatePending();
            response.restartRequired = response.offlinePending;
            response.offlineError = lastOfflineUpdateError();
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

    namespace {
        /// collects what failed, so one failing package manager doesn't stop the others
        class Failures {
        public:
            void add(const QString &message) { m_messages.append(message); }

            /// true if nothing failed, otherwise sets @p error to every failure, one per line
            bool report(QString *error) const {
                if (m_messages.isEmpty()) {
                    return true;
                }
                *error = m_messages.join(u'\n');
                return false;
            }

        private:
            QStringList m_messages;
        };

        /// "dnf upgrade" (or its --downloadonly part), false with @p error if it can't or didn't work
        bool upgradeRpms(const QString &dnf, const bool assumeYes, const bool offline, QString *error) {
            QStringList arguments{u"upgrade"_s, u"--refresh"_s};
            if (offline) {
                // checked first, so nothing is downloaded for nothing
                if (packageKitClient().isEmpty()) {
                    *error = u"Installing updates while restarting needs PackageKit, which isn't installed"_s;
                    return false;
                }
                // PackageKit would replace it
                if (const SystemUpdateTrigger trigger = systemUpdateTrigger();
                    trigger == SystemUpdateTrigger::PackageKitUpgrade || trigger == SystemUpdateTrigger::Other) {
                    *error = trigger == SystemUpdateTrigger::PackageKitUpgrade
                        ? u"A system upgrade is already scheduled to install while restarting"_s
                        : u"Another offline update is already scheduled (%1 exists)"_s.arg(PackageKitPaths().trigger);
                    return false;
                }
                // download into PackageKit's cache, which then prepares the update from
                // the same metadata and packages. dnf (as root) uses cachedir.
                arguments.append(u"--downloadonly"_s);
                if (const QString cacheDir = packageKitCacheDir(); !cacheDir.isEmpty()) {
                    arguments.append(u"--setopt=cachedir="_s + cacheDir);
                }
            }
            if (assumeYes) {
                arguments.append(u"-y"_s);
            }
            if (const int exitCode = runForwarded(dnf, arguments); exitCode != 0) {
                *error = u"RPM update failed (exit code %1)"_s.arg(exitCode);
                return false;
            }
            // dnf has finished (and released its locks) before PackageKit starts.
            // Only prepared if the download worked, so it never installs a partial download.
            return !offline || prepareOfflineUpdate(error);
        }
    } // namespace

    bool applyUser(const bool assumeYes, QString *error) {
        Failures failures;
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            QStringList arguments{u"update"_s, u"--user"_s};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            if (const int exitCode = runForwarded(flatpak, arguments); exitCode != 0) {
                failures.add(u"Flatpak user update failed (exit code %1)"_s.arg(exitCode));
            }
        }
        // `nix profile upgrade` doesn't prompt
        if (canManageNixProfile()) {
            if (const int exitCode = upgradeNixProfile(); exitCode != 0) {
                failures.add(u"Nix profile upgrade failed (exit code %1)"_s.arg(exitCode));
            }
        }
        if (canManagePlasmoids()) {
            if (QString plasmoidError; !upgradePlasmoids(&plasmoidError)) {
                failures.add(u"Plasma widget update failed: %1"_s.arg(plasmoidError));
            }
        }
        return failures.report(error);
    }

    bool applySystem(const bool assumeYes, const bool offline, bool *updated, QString *error) {
        *updated = false;
        Failures failures;
        if (isImageMode()) {
            // the new image is always staged for the next boot, so offline doesn't apply
            if (QString imageError; !upgradeImage(&imageError)) {
                failures.add(imageError);
            }
            *updated = true;
        } else if (const QString dnf = QStandardPaths::findExecutable(u"dnf"_s); !dnf.isEmpty()) {
            if (QString rpmError; !upgradeRpms(dnf, assumeYes, offline, &rpmError)) {
                failures.add(rpmError);
            }
            *updated = true;
        }
        if (!determinateNixdBinary().isEmpty()) {
            // Nix itself, installed system-wide by the nix tweak. Only upgrades
            // when Determinate Systems advises a newer version.
            if (const int exitCode = upgradeDeterminateNix(); exitCode != 0) {
                failures.add(u"Determinate Nix upgrade failed (exit code %1)"_s.arg(exitCode));
            }
            *updated = true;
        }
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            QStringList arguments{u"update"_s, u"--system"_s};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            if (const int exitCode = runForwarded(flatpak, arguments); exitCode != 0) {
                failures.add(u"Flatpak update failed (exit code %1)"_s.arg(exitCode));
            }
            *updated = true;
        }
        return failures.report(error);
    }

} // namespace UltramarineUpdates
