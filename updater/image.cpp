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
            QFile osRelease(u"/etc/os-release"_s);
            if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
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

        /// Parses `bootc status --format=json` into the pending update (status.cachedUpdate).
        std::optional<PackageUpdate> parseBootcUpdate(const QByteArray &json, const QString &name) {
            const QJsonObject status = QJsonDocument::fromJson(json).object().value("status"_L1).toObject();
            const QJsonObject cached = status.value("cachedUpdate"_L1).toObject();
            if (cached.isEmpty()) {
                return std::nullopt;
            }
            const QJsonObject booted = status.value("booted"_L1).toObject().value("image"_L1).toObject();
            const QString oldVersion = versionOf(booted, u"imageDigest"_s);
            const QString newVersion = versionOf(cached, u"imageDigest"_s);
            if (booted.value("imageDigest"_L1) == cached.value("imageDigest"_L1)) {
                return std::nullopt;
            }
            const QString image = cached.value("image"_L1).toObject().value("image"_L1).toString();
            return imageUpdate(name, u"bootc"_s, image.isEmpty() ? u"Image"_s : u"Image (%1)"_s.arg(image), oldVersion,
                               newVersion);
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

    std::optional<PackageUpdate> parseRpmOstreeStatus(const QByteArray &json, const QString &name,
                                                      const QString &backend) {
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        const QJsonObject cached = root.value("cached-update"_L1).toObject();
        if (cached.isEmpty()) {
            return std::nullopt;
        }
        QJsonObject booted;
        for (const QJsonArray deployments = root.value("deployments"_L1).toArray(); const auto &value: deployments) {
            if (value.toObject().value("booted"_L1).toBool()) {
                booted = value.toObject();
                break;
            }
        }
        if (!booted.isEmpty() && booted.value("checksum"_L1) == cached.value("checksum"_L1)) {
            return std::nullopt;
        }
        QString origin = cached.value("origin"_L1).toString();
        if (origin.isEmpty()) {
            origin = booted.value("container-image-reference"_L1).toString();
        }
        return imageUpdate(name, backend, origin.isEmpty() ? u"Image"_s : u"Image (%1)"_s.arg(origin),
                           versionOf(booted, u"checksum"_s), versionOf(cached, u"checksum"_s));
    }

    QList<PackageUpdate> imageUpdates(QString *error) {
        // Neither tool checks the registry here: that needs root and happens when
        // bootc/rpm-ostree check for updates on their own (or before installing).
        // We report the update they already found.
        const QString name = osName();
        if (const QString bootc = QStandardPaths::findExecutable(u"bootc"_s); !bootc.isEmpty()) {
            if (const CommandResult result = capture(bootc, {u"status"_s, u"--format=json"_s}); result.succeeded()) {
                if (const std::optional<PackageUpdate> update = parseBootcUpdate(result.out, name)) {
                    return {*update};
                }
                return {};
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
        if (const std::optional<PackageUpdate> update = parseRpmOstreeStatus(result.out, name, backend)) {
            return {*update};
        }
        return {};
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
