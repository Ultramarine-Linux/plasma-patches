// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QUrl>

#include <functional>

namespace OfficeInstaller {

    /// Our desktop file's storage ID, skipped when looking for an installed suite.
    inline constexpr auto DesktopFileId = "org.ultramarine.OfficeInstaller.desktop";

    /**
     * Opens each of @p urls with an installed app that lists its MIME type
     * itself, not through a parent type: a .docx is a zip, but Ark isn't an
     * office suite. Returns false without opening anything if there are no
     * URLs or any URL has no such app. Otherwise calls @p done in @p context once every app has started.
     */
    bool openWithInstalledApps(const QList<QUrl> &urls, const QObject *context, const std::function<void()> &done);

} // namespace OfficeInstaller
