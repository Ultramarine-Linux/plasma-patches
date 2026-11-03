// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KService>

#include <QUrl>

#include <functional>

namespace OfficeInstaller {

    /// our desktop file's storage ID, skipped when looking for an installed suite
    inline constexpr auto DesktopFileId = "org.ultramarine.OfficeInstaller.desktop";

    /**
     * opens each of @p urls with an installed app that lists its MIME type
     * itself, not through a parent type: a .docx is a zip, but Ark isn't an
     * office suite. Returns false without opening anything if there are no
     * URLs or any URL has no such app. Otherwise calls @p done in @p context once
     * every app has started or failed to, with the errors (empty if there were none).
     */
    bool openWithInstalledApps(const QList<QUrl> &urls, const QObject *context,
                               const std::function<void(const QString &error)> &done);

    /**
     * an installed office suite, found by what opens the common document,
     * spreadsheet and presentation formats. Null if there's none.
     */
    KService::Ptr findInstalledSuite();

} // namespace OfficeInstaller
