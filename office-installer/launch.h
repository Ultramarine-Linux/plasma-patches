// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KService>

#include <QMimeType>
#include <QUrl>

#include <functional>

namespace OfficeInstaller {

    inline constexpr auto OwnDesktopFileId = "org.ultramarine.OfficeInstaller.desktop";

    using LaunchesFinished = std::function<void(const QString &errors, const QList<QUrl> &failedUrls)>;

    bool openWithInstalledApps(const QList<QUrl> &urls, const QObject *context, const LaunchesFinished &done);

    KService::Ptr findInstalledSuite();

    QMimeType mimeTypeOf(const QUrl &url);

} // namespace OfficeInstaller
