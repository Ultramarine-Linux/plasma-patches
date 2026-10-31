// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>

#include "updates.h"

/**
 * Image mode (bootc/rpm-ostree) systems. The OS is one image, so dnf can't
 * update it: bootc (or rpm-ostree) stages the new image for the next boot.
 * Systems with a local `um env` derivation track localhost/um-env and must be
 * rebuilt through `um env update`, which also pulls the new base image.
 */
namespace UltramarineUpdates {

    /// The image a bootc system tracks, from `bootc status --format=json`.
    struct BootcStatus {
        /// e.g. "ghcr.io/ultramarine-linux/plasma-bootc:44". Empty if not tracking an image.
        QString image;
    };

    /// Whether the system was booted from an ostree deployment (bootc or rpm-ostree).
    bool isImageMode();

    /// Parses `bootc status --format=json`.
    std::optional<BootcStatus> parseBootcStatus(const QByteArray &json);

    /// Whether @p image is the local derivation managed by `um env`.
    bool isUmEnvImage(const QString &image);

    /// What an image mode system has pending.
    struct ImageState {
        /// A newer image bootc (or rpm-ostree) found that isn't staged yet.
        std::optional<PackageUpdate> update;
        /// Whether an image is staged and becomes the system when restarting.
        bool staged = false;
        /// The staged image's version (or short digest).
        QString stagedVersion;
    };

    /**
     * Parses `bootc status --format=json`. The update is the booted entry's
     * cachedUpdate, unless that image is already staged. Download-only stages
     * aren't applied when restarting, so they still count as an update.
     * @p name is shown as the update's name.
     */
    ImageState parseBootcState(const QByteArray &json, const QString &name);

    /**
     * Parses `rpm-ostree status --json`: the "cached-update", unless it's what's
     * booted or the staged deployment. @p backend is "bootc" or "rpm-ostree".
     */
    ImageState parseRpmOstreeState(const QByteArray &json, const QString &name, const QString &backend);

    /// Reports a pending or staged image update. Needs no privileges, so it only
    /// knows about updates bootc (or rpm-ostree) already found; see checkImageUpdate().
    ImageState imageState(QString *error);

    /// Looks for a new image in the registry without downloading or staging it,
    /// so imageState() can report it. Needs root.
    bool checkImageUpdate(QString *error);

    /// Stages the new image for the next boot. Needs root.
    bool upgradeImage(QString *error);

} // namespace UltramarineUpdates
