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

    /**
     * Parses `rpm-ostree status --json` into the pending image update, if
     * rpm-ostree has already found one (its "cached-update"). @p name is shown as
     * the update's name, @p backend is "bootc" or "rpm-ostree".
     */
    std::optional<PackageUpdate> parseRpmOstreeStatus(const QByteArray &json, const QString &name,
                                                      const QString &backend);

    /// Lists a pending image update. Needs no privileges.
    QList<PackageUpdate> imageUpdates(QString *error);

    /// Stages the new image for the next boot. Needs root.
    bool upgradeImage(QString *error);

} // namespace UltramarineUpdates
