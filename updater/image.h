// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>

#include "updates.h"

namespace UltramarineUpdates {

    struct BootcStatus {
        QString image;
    };

    bool isImageMode();

    std::optional<BootcStatus> parseBootcStatus(const QByteArray &json);

    bool isUmEnvImage(const QString &image);

    struct ImageState {
        std::optional<PackageUpdate> update;
        bool staged = false;
        QString stagedVersion;
    };

    ImageState parseBootcState(const QByteArray &json, const QString &name);

    ImageState parseRpmOstreeState(const QByteArray &json, const QString &name, const QString &backend);

    QByteArray bootcStatusCache(const QByteArray &status, const QString &bootId);

    std::optional<QByteArray> parseBootcStatusCache(const QByteArray &cache, const QString &bootId);

    void updateBootcStatusCache();

    ImageState imageState(QString *error);

    bool checkImageUpdate(QString *error);

    bool upgradeImage(QString *error);

} // namespace UltramarineUpdates
