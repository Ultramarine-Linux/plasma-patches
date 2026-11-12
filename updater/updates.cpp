// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "updates.h"

#include <KLocalizedString>

#include <future>
#include <optional>
#include <vector>

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

        struct SourceResult {
            QString source;
            QList<PackageUpdate> updates;
            QString error;
        };

        template<typename Query>
        SourceResult querySource(QString source, Query query) {
            SourceResult result;
            result.source = std::move(source);
            result.updates = query(&result.error);
            return result;
        }

        template<typename Query>
        std::future<SourceResult> querySourceAsync(QString source, Query query) {
            return std::async(std::launch::async, [source = std::move(source), query = std::move(query)]() mutable {
                return querySource(std::move(source), std::move(query));
            });
        }
    } // namespace

    ListResponse listUpdates() {
        std::vector<std::future<SourceResult>> pending;
        ListResponse response;

        const bool imageMode = isImageMode();
        ImageState image;
        if (imageMode) {
            // dnf exists in bootc images, but can't update the image
            pending.push_back(
                querySourceAsync(i18nc("@item an update source", "system image"), [&image](QString *error) {
                    image = imageState(error);
                    return image.update ? QList<PackageUpdate>{*image.update} : QList<PackageUpdate>{};
                }));
        } else if (hasExecutable(u"dnf"_s)) {
            pending.push_back(querySourceAsync(i18nc("@item an update source", "RPM packages"), rpmPackageUpdates));
            response.offlinePending = offlineUpdatePending();
            response.restartRequired = response.offlinePending;
            response.offlineError = lastOfflineUpdateError();
        }
        if (!determinateNixdBinary().isEmpty()) {
            pending.push_back(
                querySourceAsync(i18nc("@item an update source", "Determinate Nix"), determinateNixUpdate));
        }
        if (canManageNixProfile()) {
            pending.push_back(querySourceAsync(i18nc("@item an update source", "your Nix profile"), nixPackageUpdates));
        }
        if (hasExecutable(u"flatpak"_s)) {
            for (const QString &installation: flatpakInstallations()) {
                pending.push_back(querySourceAsync(
                    i18nc("@item an update source, %1 is a Flatpak installation like user or system", "Flatpak (%1)",
                          installation),
                    [installation](QString *error) { return flatpakPackageUpdates(installation, error); }));
            }
        }

        // KNewStuff runs event loops and answers questions on the main thread
        std::optional<SourceResult> plasmoids;
        if (canManagePlasmoids()) {
            plasmoids = querySource(i18nc("@item an update source", "Plasma widgets"), plasmoidUpdates);
        }

        const auto collect = [&response](const SourceResult &result) {
            ++response.queriedSources;
            if (!result.error.isEmpty()) {
                response.errors.append(i18nc("@info:status %1 is an update source, %2 its error message", "%1: %2",
                                             result.source, result.error));
                return;
            }
            response.updates.append(result.updates);
        };
        for (std::future<SourceResult> &source: pending) {
            collect(source.get());
        }
        if (plasmoids) {
            collect(*plasmoids);
        }
        if (imageMode) {
            response.restartRequired = image.staged;
            response.stagedVersion = image.stagedVersion;
        }
        return response;
    }

    void ApplyResult::fail(const QString &message, const bool primary) {
        failures.append(message);
        primaryFailed = primaryFailed || primary;
    }

    namespace {
        /// "dnf upgrade" (or its --downloadonly part), false with @p error if it can't or didn't work
        bool upgradeRpms(const QString &dnf, const bool assumeYes, const bool offline, QString *error) {
            QStringList arguments{u"upgrade"_s, u"--refresh"_s};
            if (offline) {
                // checked first, so nothing is downloaded for nothing
                if (packageKitClient().isEmpty()) {
                    *error = i18nc("@info:status",
                                   "Installing updates while restarting needs PackageKit, which isn't installed.");
                    return false;
                }
                // PackageKit would replace it
                if (const SystemUpdateTrigger trigger = systemUpdateTrigger();
                    trigger == SystemUpdateTrigger::PackageKitUpgrade || trigger == SystemUpdateTrigger::Other) {
                    *error = trigger == SystemUpdateTrigger::PackageKitUpgrade
                        ? i18nc("@info:status", "A system upgrade is already scheduled to install while restarting.")
                        : i18nc("@info:status %1 is a file path",
                                "Another offline update is already scheduled (%1 exists).", PackageKitPaths().trigger);
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
                *error = i18nc("@info:status", "Updating RPM packages failed (exit code %1).", exitCode);
                return false;
            }
            // dnf has finished (and released its locks) before PackageKit starts.
            // Only prepared if the download worked, so it never installs a partial download.
            return !offline || prepareOfflineUpdate(error);
        }
    } // namespace

    ApplyResult applyUser(const bool assumeYes) {
        ApplyResult result;
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            result.updated = true;
            QStringList arguments{u"update"_s, u"--user"_s};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            if (const int exitCode = runForwarded(flatpak, arguments); exitCode != 0) {
                result.fail(i18nc("@info:status", "Updating your Flatpaks failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        // `nix profile upgrade` doesn't prompt
        if (canManageNixProfile()) {
            result.updated = true;
            if (const int exitCode = upgradeNixProfile(); exitCode != 0) {
                result.fail(i18nc("@info:status", "Updating your Nix profile failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        if (canManagePlasmoids()) {
            result.updated = true;
            if (QString plasmoidError; !upgradePlasmoids(&plasmoidError)) {
                result.fail(
                    i18nc("@info:status %1 is an error message", "Updating Plasma widgets failed: %1", plasmoidError));
            } else {
                result.succeed();
            }
        }
        return result;
    }

    ApplyResult applySystem(const bool assumeYes, const bool offline) {
        ApplyResult result;
        if (isImageMode()) {
            // the new image is always staged for the next boot, so offline doesn't apply
            result.updated = true;
            if (QString imageError; !upgradeImage(&imageError)) {
                result.fail(imageError, true);
            } else {
                result.succeed();
            }
        } else if (const QString dnf = QStandardPaths::findExecutable(u"dnf"_s); !dnf.isEmpty()) {
            result.updated = true;
            if (QString rpmError; !upgradeRpms(dnf, assumeYes, offline, &rpmError)) {
                result.fail(rpmError, true);
            } else {
                result.succeed();
            }
        }
        if (!determinateNixdBinary().isEmpty()) {
            // Nix itself, installed system-wide by the nix tweak. Only upgrades
            // when Determinate Systems advises a newer version.
            result.updated = true;
            if (const int exitCode = upgradeDeterminateNix(); exitCode != 0) {
                result.fail(i18nc("@info:status", "Updating Determinate Nix failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            result.updated = true;
            QStringList arguments{u"update"_s, u"--system"_s};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            if (const int exitCode = runForwarded(flatpak, arguments); exitCode != 0) {
                result.fail(i18nc("@info:status", "Updating system Flatpaks failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        return result;
    }

} // namespace UltramarineUpdates
