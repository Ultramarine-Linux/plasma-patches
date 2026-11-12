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
            const auto flatpakSource = [](const QString &installation) {
                return querySourceAsync(
                    i18nc("@item an update source, %1 is a Flatpak installation like user or system", "Flatpak (%1)",
                          installation),
                    [installation](QString *error) { return flatpakPackageUpdates(installation, error); });
            };
            pending.push_back(flatpakSource(u"system"_s));
            pending.push_back(flatpakSource(u"user"_s));
        }

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
        QString reasonOfflineUpdateIsImpossible() {
            if (packageKitClient().isEmpty()) {
                return i18nc("@info:status",
                             "Installing updates while restarting needs PackageKit, which isn't installed.");
            }
            switch (systemUpdateTrigger()) {
            case SystemUpdateTrigger::PackageKitUpgrade:
                return i18nc("@info:status", "A system upgrade is already scheduled to install while restarting.");
            case SystemUpdateTrigger::Other:
                return i18nc("@info:status %1 is a file path",
                             "Another offline update is already scheduled (%1 exists).", PackageKitPaths().trigger);
            case SystemUpdateTrigger::None:
            case SystemUpdateTrigger::PackageKitUpdate:
                break;
            }
            return {};
        }

        QStringList downloadIntoPackageKitCacheArguments() {
            QStringList arguments{u"--downloadonly"_s};
            if (const QString cacheDir = packageKitCacheDir(); !cacheDir.isEmpty()) {
                arguments.append(u"--setopt=cachedir="_s + cacheDir);
            }
            return arguments;
        }

        int updateFlatpaks(const QString &flatpak, const QString &installationFlag, const bool assumeYes) {
            QStringList arguments{u"update"_s, installationFlag};
            if (assumeYes) {
                arguments.append(u"--noninteractive"_s);
            }
            return runForwarded(flatpak, arguments);
        }

        bool upgradeRpms(const QString &dnf, const bool assumeYes, const bool offline, QString *error) {
            QStringList arguments{u"upgrade"_s, u"--refresh"_s};
            if (offline) {
                if (*error = reasonOfflineUpdateIsImpossible(); !error->isEmpty()) {
                    return false;
                }
                arguments.append(downloadIntoPackageKitCacheArguments());
            }
            if (assumeYes) {
                arguments.append(u"-y"_s);
            }
            if (const int exitCode = runForwarded(dnf, arguments)) {
                *error = i18nc("@info:status", "Updating RPM packages failed (exit code %1).", exitCode);
                return false;
            }
            return !offline || prepareOfflineUpdate(error);
        }
    } // namespace

    ApplyResult applyUser(const bool assumeYes) {
        ApplyResult result;
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            result.updated = true;
            if (const int exitCode = updateFlatpaks(flatpak, u"--user"_s, assumeYes)) {
                result.fail(i18nc("@info:status", "Updating your Flatpaks failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        if (canManageNixProfile()) {
            result.updated = true;
            if (const int exitCode = upgradeNixProfile()) {
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
            result.updated = true;
            if (const int exitCode = upgradeDeterminateNix()) {
                result.fail(i18nc("@info:status", "Updating Determinate Nix failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        if (const QString flatpak = QStandardPaths::findExecutable(u"flatpak"_s); !flatpak.isEmpty()) {
            result.updated = true;
            if (const int exitCode = updateFlatpaks(flatpak, u"--system"_s, assumeYes)) {
                result.fail(i18nc("@info:status", "Updating system Flatpaks failed (exit code %1).", exitCode));
            } else {
                result.succeed();
            }
        }
        return result;
    }

} // namespace UltramarineUpdates
