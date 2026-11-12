// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "image.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <unistd.h>

#include "activity.h"
#include "lock.h"
#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        QString shortDigest(QString id) {
            if (const qsizetype algorithmSeparator = id.indexOf(u':'); algorithmSeparator >= 0) {
                id = id.mid(algorithmSeparator + 1);
            }
            return id.left(12);
        }

        QString versionOf(const QJsonObject &object, const QString &digestKey) {
            if (const QString version = object.value("version"_L1).toString(); !version.isEmpty()) {
                return version;
            }
            return shortDigest(object.value(digestKey).toString());
        }

        QString osName() {
            if (QFile osRelease(u"/etc/os-release"_s); osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
                for (const QStringList lines = QString::fromUtf8(osRelease.readAll()).split(u'\n');
                     const QString &line: lines) {
                    if (constexpr auto namePrefix = "NAME="_L1; line.startsWith(namePrefix)) {
                        return line.mid(namePrefix.size()).remove(u'"');
                    }
                }
            }
            return i18nc("@item the name of the operating system when it's unknown", "System image");
        }

        QString bootcStatusCachePath() { return systemStateDir() + "/bootc-status.json"_L1; }

        void makeTraversableByEveryone(const QString &directory) {
            constexpr auto traversable = QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;
            if (const QFileDevice::Permissions permissions = QFile::permissions(directory);
                (permissions & traversable) != traversable) {
                QFile::setPermissions(directory, permissions | traversable);
            }
        }

        QString bootId() {
            QFile file(u"/proc/sys/kernel/random/boot_id"_s);
            return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll().trimmed()) : QString();
        }

        std::optional<BootcStatus> liveBootcStatus(const QString &bootc) {
            return parseBootcStatus(capture(bootc, {u"status"_s, u"--format=json"_s}).out);
        }

        QString imageCheckError(const CommandResult &result) {
            return i18nc("@info:status %1 is an error message", "Checking for a new system image failed: %1",
                         QString::fromUtf8(result.err).trimmed());
        }

        PackageUpdate imageUpdate(const QString &name, const QString &backend, const QString &origin,
                                  const QString &oldVersion, const QString &newVersion) {
            PackageUpdate update;
            update.id = u"image:system"_s;
            update.name = name;
            update.source = origin.isEmpty() ? u"Image"_s : u"Image (%1)"_s.arg(origin);
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
        QString name = image;
        if (constexpr auto localhostPrefix = "localhost/"_L1; name.startsWith(localhostPrefix)) {
            name = name.mid(localhostPrefix.size());
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
        if (const bool stageAppliesOnRestart = !staged.isEmpty() && !staged.value("downloadOnly"_L1).toBool();
            stageAppliesOnRestart && stagedDigest != bootedDigest) {
            state.staged = true;
            state.stagedVersion = versionOf(stagedImage, u"imageDigest"_s);
        }
        const QJsonObject cached = booted.value("cachedUpdate"_L1).toObject();
        if (const QString cachedDigest = cached.value("imageDigest"_L1).toString();
            cached.isEmpty() || cachedDigest == bootedDigest || (state.staged && cachedDigest == stagedDigest)) {
            return state;
        }
        const QString image = cached.value("image"_L1).toObject().value("image"_L1).toString();
        state.update = imageUpdate(name, u"bootc"_s, image, versionOf(bootedImage, u"imageDigest"_s),
                                   versionOf(cached, u"imageDigest"_s));
        return state;
    }

    ImageState parseRpmOstreeState(const QByteArray &json, const QString &name, const QString &backend) {
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QJsonObject booted;
        QJsonObject pending;
        for (const QJsonArray deployments = root.value("deployments"_L1).toArray(); const auto &value: deployments) {
            if (const QJsonObject deployment = value.toObject(); deployment.value("booted"_L1).toBool()) {
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
        if (const QJsonValue checksum = cached.value("checksum"_L1);
            (!booted.isEmpty() && booted.value("checksum"_L1) == checksum) ||
            (state.staged && pending.value("checksum"_L1) == checksum)) {
            return state;
        }
        QString origin = cached.value("origin"_L1).toString();
        if (origin.isEmpty()) {
            origin = booted.value("container-image-reference"_L1).toString();
        }
        state.update =
            imageUpdate(name, backend, origin, versionOf(booted, u"checksum"_s), versionOf(cached, u"checksum"_s));
        return state;
    }

    QByteArray bootcStatusCache(const QByteArray &status, const QString &bootId) {
        const QJsonObject cache{
            {u"boot_id"_s, bootId},
            {u"status"_s, QJsonDocument::fromJson(status).object()},
        };
        return QJsonDocument(cache).toJson(QJsonDocument::Compact) + '\n';
    }

    std::optional<QByteArray> parseBootcStatusCache(const QByteArray &cache, const QString &bootId) {
        const QJsonObject object = QJsonDocument::fromJson(cache).object();
        const QJsonValue status = object.value("status"_L1);
        if (const bool fromCurrentBoot = !bootId.isEmpty() && object.value("boot_id"_L1).toString() == bootId;
            !fromCurrentBoot || !status.isObject()) {
            return std::nullopt;
        }
        return QJsonDocument(status.toObject()).toJson(QJsonDocument::Compact);
    }

    void updateBootcStatusCache() {
        const QString bootc = QStandardPaths::findExecutable(u"bootc"_s);
        if (bootc.isEmpty() || geteuid() != 0) {
            return;
        }
        const CommandResult result = capture(bootc, {u"status"_s, u"--format=json"_s});
        InstallLock statusCacheLock(statusCacheLockPath());
        if (QString error; statusCacheLock.tryLock(&error, std::chrono::minutes(1)) != InstallLock::Result::Acquired) {
            return;
        }
        const QString path = bootcStatusCachePath();
        if (!result.succeeded() || !QJsonDocument::fromJson(result.out).isObject()) {
            QFile::remove(path);
            return;
        }
        const QString directory = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(directory)) {
            return;
        }
        makeTraversableByEveryone(directory);
        const QByteArray cache = bootcStatusCache(result.out, bootId());
        if (QFile existing(path); existing.open(QIODevice::ReadOnly) && existing.readAll() == cache &&
            existing.permissions().testFlag(QFile::ReadOther)) {
            return;
        }
        if (QSaveFile file(path);
            file.open(QIODevice::WriteOnly) && file.write(cache) == cache.size() && file.commit()) {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        }
    }

    ImageState imageState(QString *error) {
        const QString name = osName();
        const QString bootc = QStandardPaths::findExecutable(u"bootc"_s);
        if (!bootc.isEmpty()) {
            std::optional<QByteArray> status;
            if (const CommandResult result = capture(bootc, {u"status"_s, u"--format=json"_s}); result.succeeded()) {
                status = result.out;
            } else if (QFile cache(bootcStatusCachePath()); cache.open(QIODevice::ReadOnly)) {
                status = parseBootcStatusCache(cache.readAll(), bootId());
            }
            if (const std::optional<BootcStatus> parsed = status ? parseBootcStatus(*status) : std::nullopt;
                parsed && !parsed->image.isEmpty()) {
                return parseBootcState(*status, name);
            }
        }
        const QString rpmOstree = QStandardPaths::findExecutable(u"rpm-ostree"_s);
        if (rpmOstree.isEmpty()) {
            return {};
        }
        const CommandResult result = capture(rpmOstree, {u"status"_s, u"--json"_s});
        if (!result.succeeded()) {
            *error = i18nc("@info:status %1 is an error message", "rpm-ostree status failed: %1",
                           QString::fromUtf8(result.err).trimmed());
            return {};
        }
        return parseRpmOstreeState(result.out, name, bootc.isEmpty() ? u"rpm-ostree"_s : u"bootc"_s);
    }

    bool checkImageUpdate(QString *error) {
        if (const QString bootc = QStandardPaths::findExecutable(u"bootc"_s); !bootc.isEmpty()) {
            if (const std::optional<BootcStatus> status = liveBootcStatus(bootc); status && !status->image.isEmpty()) {
                if (isUmEnvImage(status->image)) {
                    return true;
                }
                if (const CommandResult result = capture(bootc, {u"upgrade"_s, u"--check"_s}); !result.succeeded()) {
                    *error = imageCheckError(result);
                    return false;
                }
                return true;
            }
        }
        const QString rpmOstree = QStandardPaths::findExecutable(u"rpm-ostree"_s);
        if (rpmOstree.isEmpty()) {
            *error = i18nc("@info:status", "Neither bootc nor rpm-ostree is installed.");
            return false;
        }
        constexpr int rpmOstreeNoUpdateExitCode = 77;
        if (const CommandResult result = capture(rpmOstree, {u"upgrade"_s, u"--check"_s});
            !result.started || (result.exitCode != 0 && result.exitCode != rpmOstreeNoUpdateExitCode)) {
            *error = imageCheckError(result);
            return false;
        }
        return true;
    }

    bool upgradeImage(QString *error) {
        if (const QString bootc = QStandardPaths::findExecutable(u"bootc"_s); !bootc.isEmpty()) {
            if (const std::optional<BootcStatus> status = liveBootcStatus(bootc); status && !status->image.isEmpty()) {
                if (isUmEnvImage(status->image)) {
                    const QString um = QStandardPaths::findExecutable(u"um"_s);
                    if (um.isEmpty()) {
                        *error = i18nc("@info:status", "This system uses um env, but um is not installed.");
                        return false;
                    }
                    if (const int exitCode = runForwarded(um, {u"env"_s, u"update"_s})) {
                        *error = i18nc("@info:status", "Updating the um env image failed (exit code %1).", exitCode);
                        return false;
                    }
                } else if (const int exitCode = runForwarded(bootc, {u"upgrade"_s})) {
                    *error = i18nc("@info:status", "Updating the system image failed (exit code %1).", exitCode);
                    return false;
                }
                updateBootcStatusCache();
                return true;
            }
        }
        const QString rpmOstree = QStandardPaths::findExecutable(u"rpm-ostree"_s);
        if (rpmOstree.isEmpty()) {
            *error = i18nc("@info:status", "Neither bootc nor rpm-ostree is installed.");
            return false;
        }
        if (const int exitCode = runForwarded(rpmOstree, {u"upgrade"_s})) {
            *error = i18nc("@info:status", "Updating the system image failed (exit code %1).", exitCode);
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
