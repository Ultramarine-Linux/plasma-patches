// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>

namespace OfficeInstaller {

    /**
     * installs a suite in the background. The command line tools can't do this
     * from a GUI: flatpak -y, pkcon -y and pkgcli -y also mark the transaction
     * non-interactive, so polkit refuses non-admin users instead of asking for a
     * password, and without -y they wait for an answer on a terminal. The
     * subclasses use the libraries and D-Bus APIs instead, with interaction allowed.
     */
    class Installer : public QObject {
        Q_OBJECT

    public:
        using QObject::QObject;

        virtual void start() = 0;
        /// stops the install. finished() follows once it has stopped
        virtual void cancel() = 0;

    Q_SIGNALS:
        /// a line of progress, like the package being downloaded
        void progress(const QString &text);
        /// @p error may be empty when @p success is false. @p canceled is set if cancel() stopped it
        void finished(bool success, bool canceled, const QString &error);
    };

} // namespace OfficeInstaller
