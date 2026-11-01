// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "image.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        /// A short, recognizable label for an image without a version: its digest or checksum.
        QString shortId(QString id) {
            if (const qsizetype colon = id.indexOf(u':'); colon >= 0) {
                id = id.mid(colon + 1); // "sha256:..."
            }
            return id.left(12);
        }

        QString versionOf(const QJsonObject &object, const QString &digestKey) {
            if (const QString version = object.value("version"_L1).toString(); !version.isEmpty()) {
                return version;
            }
            return shortId(object.value(digestKey).toString());
        }

        QString osName() {
            if (QFile osRelease(u"/etc/os-release"_s); osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
                for (const QStringList lines = QString::fromUtf8(osRelease.readAll()).split(u'\n');
                     const QString &line: lines) {
                    if (line.startsWith("NAME="_L1)) {
                        return line.mid(5).remove(u'"');
                    }
                }
            }
            return u"System image"_s;
        }

        PackageUpdate imageUpdate(const QString &name, const QString &backend, const QString &source,
                                  const QString &oldVersion, const QString &newVersion) {
            PackageUpdate update;
            update.id = u"image:system"_s;
            update.name = name;
            update.source = source;
            update.backend = backend;
            update.kind = QString::fromLatin1(Kind::Package);
            update.icon = u"system-software-update"_s;
            update.oldVersion = oldVersion;
            update.newVersion = newVersion;
            return update;
        }
    } // namespace

    bool isImageMode() { return QFile::exists(u"/run/ostree-booted"_s); }

    std::optional<BootcStatus> parseBootcStatus(const QByteArray &json) {
        const QJsonDocument document = QJsonDocument::fromJson(json);
        if (!document.isObject()) {
            return std::nullopt;
        }
        const QJsonObject spec = document.object().value("spec"_L1).toObject();
        return BootcStatus{.image = spec.value("image"_L1).toObject().value("image"_L1).toString()};
    }

    bool isUmEnvImage(const QString &image) {
        // `um env` switches the system to localhost/um-env, built FROM the real image.
        QString name = image;
        if (name.startsWith("localhost/"_L1)) {
            name = name.mid(10);
        }
        return name == "um-env"_L1 || name.startsWith("um-env:"_L1);
    }

    ImageState parseBootcState(const QByteArray &json, const QString &name) {
        const QJsonObject status = QJsonDocument::fromJson(json).object().value("status"_L1).toObject();
        const QJsonObject booted = status.value("booted"_L1).toObject();
        const QJsonObject bootedImage = booted.value("image"_L1).toObject();
        const QJsonObject staged = status.value("staged"_L1).toObject();
        const QJsonObject stagedImage = staged.value("image"_L1).toObject();
        const QString bootedDigest = bootedImage.value("imageDigest"_L1).toString();
        const QString stagedDigest = stagedImage.value("imageDigest"_L1).toString();

        ImageState state;
        // A download-only stage stays locked until `bootc upgrade` unlocks it.
        if (!staged.isEmpty() && !staged.value("downloadOnly"_L1).toBool() && stagedDigest != bootedDigest) {
            state.staged = true;
            state.stagedVersion = versionOf(stagedImage, u"imageDigest"_s);
        }
        const QJsonObject cached = booted.value("cachedUpdate"_L1).toObject();
        const QString cachedDigest = cached.value("imageDigest"_L1).toString();
        if (cached.isEmpty() || cachedDigest == bootedDigest || (state.staged && cachedDigest == stagedDigest)) {
            return state;
        }
        const QString image = cached.value("image"_L1).toObject().value("image"_L1).toString();
        state.update = imageUpdate(name, u"bootc"_s, image.isEmpty() ? u"Image"_s : u"Image (%1)"_s.arg(image),
                                   versionOf(bootedImage, u"imageDigest"_s), versionOf(cached, u"imageDigest"_s));
        return state;
    }

    ImageState parseRpmOstreeState(const QByteArray &json, const QString &name, const QString &backend) {
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        // Upgrades stage a deployment that's finalized when shutting down. A
        // deployment that isn't staged but comes before the booted one is a rollback.
        QJsonObject booted;
        QJsonObject pending;
        for (const QJsonArray deployments = root.value("deployments"_L1).toArray(); const auto &value: deployments) {
            const QJsonObject deployment = value.toObject();
            if (deployment.value("booted"_L1).toBool()) {
                booted = deployment;
            } else if (deployment.value("staged"_L1).toBool()) {
                pending = deployment;
            }
        }

        ImageState state;
        if (!pending.isEmpty()) {
            state.staged = true;
            state.stagedVersion = versionOf(pending, u"checksum"_s);
        }
        const QJsonObject cached = root.value("cached-update"_L1).toObject();
        if (cached.isEmpty()) {
            return state;
        }
        const QJsonValue checksum = cached.value("checksum"_L1);
        if ((!booted.isEmpty() && booted.value("checksum"_L1) == checksum) ||
            (state.staged && pending.value("checksum"_L1) == checksum)) {
            return state;
        }
        QString origin = cached.value("origin"_L1).toString();
        if (origin.isEmpty()) {
            origin = booted.value("container-image-reference"_L1).toString();
        }
        state.update = imageUpdate(name, backend, origin.isEmpty() ? u"Image"_s : u"Image (%1)"_s.arg(origin),
                                   versionOf(booted, u"checksum"_s), versionOf(cached, u"checksum"_s));
        return state;
    }

    ImageState imageState(QString *error) {
        // Neither tool checks the registry here: that needs root and happens in
        // checkImageUpdate() (or before installing). We report what they already found.
        const QString name = osName();
        if (const QString bootc = QStandardPaths::findExecutable(u"bootc"_s); !bootc.isEmpty()) {
            if (const CommandResult result = capture(bootc, {u"status"_s, u"--format=json"_s}); result.succeeded()) {
                return parseBootcState(result.out, name);
            }
        }
        const QString rpmOstree = QStandardPaths::findExecutable(u"rpm-ostree"_s);
        if (rpmOstree.isEmpty()) {
            return {};
        }
        const CommandResult result = capture(rpmOstree, {u"status"_s, u"--json"_s});
        if (!result.succeeded()) {
            *error = u"rpm-ostree status failed: %1"_s.arg(QString::fromUtf8(result.err).trimmed());
            return {};
        }
        const QString backend = QStandardPaths::findExecutable(u"bootc"_s).isEmpty() ? u"rpm-ostree"_s : u"bootc"_s;
        return parseRpmOstreeState(result.out, name, backend);
    }

    bool checkImageUpdate(QString *error) {
        if (const QString bootc = QStandardPaths::findExecutable(u"bootc"_s); !bootc.isEmpty()) {
            const CommandResult status = capture(bootc, {u"status"_s, u"--format=json"_s});
            const std::optional<BootcStatus> parsed = parseBootcStatus(status.out);
            if (parsed && isUmEnvImage(parsed->image)) {
                // The local derivation has no registry to check; `um env update`
                // pulls the new base image when installing.
                return true;
            }
            if (parsed && !parsed->image.isEmpty()) {
                if (const CommandResult result = capture(bootc, {u"upgrade"_s, u"--check"_s}); !result.succeeded()) {
                    *error = u"bootc upgrade --check failed: %1"_s.arg(QString::fromUtf8(result.err).trimmed());
                    return false;
                }
                return true;
            }
        }
        const QString rpmOstree = QStandardPaths::findExecutable(u"rpm-ostree"_s);
        if (rpmOstree.isEmpty()) {
            *error = u"neither bootc nor rpm-ostree is installed"_s;
            return false;
        }
        // Exits with 77 when there's no update.
        if (const CommandResult result = capture(rpmOstree, {u"upgrade"_s, u"--check"_s});
            !result.started || (result.exitCode != 0 && result.exitCode != 77)) {
            *error = u"rpm-ostree upgrade --check failed: %1"_s.arg(QString::fromUtf8(result.err).trimmed());
            return false;
        }
        return true;
    }

    bool upgradeImage(QString *error) {
        if (const QString bootc = QStandardPaths::findExecutable(u"bootc"_s); !bootc.isEmpty()) {
            const CommandResult status = capture(bootc, {u"status"_s, u"--format=json"_s});
            const std::optional<BootcStatus> parsed = parseBootcStatus(status.out);
            if (parsed && isUmEnvImage(parsed->image)) {
                // Rebuilds the derivation on top of the new base image, then runs bootc.
                const QString um = QStandardPaths::findExecutable(u"um"_s);
                if (um.isEmpty()) {
                    *error = u"this system uses um env, but um is not installed"_s;
                    return false;
                }
                if (const int exitCode = runForwarded(um, {u"env"_s, u"update"_s}); exitCode != 0) {
                    *error = u"um env update failed (exit code %1)"_s.arg(exitCode);
                    return false;
                }
                return true;
            }
            if (parsed && !parsed->image.isEmpty()) {
                if (const int exitCode = runForwarded(bootc, {u"upgrade"_s}); exitCode != 0) {
                    *error = u"bootc upgrade failed (exit code %1)"_s.arg(exitCode);
                    return false;
                }
                return true;
            }
        }
        // Not tracking a bootc image (or no bootc): a classic rpm-ostree system.
        const QString rpmOstree = QStandardPaths::findExecutable(u"rpm-ostree"_s);
        if (rpmOstree.isEmpty()) {
            *error = u"neither bootc nor rpm-ostree is installed"_s;
            return false;
        }
        if (const int exitCode = runForwarded(rpmOstree, {u"upgrade"_s}); exitCode != 0) {
            *error = u"rpm-ostree upgrade failed (exit code %1)"_s.arg(exitCode);
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
