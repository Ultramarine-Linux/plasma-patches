// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include "installer.h"

class QThread;
// GLib's own typedef. Including gio.h here would clash with Qt's "signals" keyword
using GCancellable = struct _GCancellable; // NOLINT(bugprone-reserved-identifier)

namespace OfficeInstaller {

    /**
     * installs an app into the system installation with libflatpak, on a worker
     * thread: a transaction blocks until it's done. Flatpak's system helper asks
     * polkit with interaction allowed, so non-admin users get a password prompt.
     */
    class FlatpakInstaller : public Installer {
        Q_OBJECT

    public:
        /// @p ref is a full ref, like app/org.example.App/x86_64/stable
        FlatpakInstaller(const QString &remote, const QString &ref, QObject *parent = nullptr);
        /// cancels a running install and waits for it to stop
        ~FlatpakInstaller() override;

        FlatpakInstaller(const FlatpakInstaller &) = delete;
        FlatpakInstaller &operator=(const FlatpakInstaller &) = delete;
        FlatpakInstaller(FlatpakInstaller &&) = delete;
        FlatpakInstaller &operator=(FlatpakInstaller &&) = delete;

        void start() override;
        void cancel() override;

        /// Flatpak's name for this machine's architecture
        static QString defaultArch();

    private:
        QString m_remote;
        QString m_ref;
        QThread *m_thread = nullptr;
        GCancellable *m_cancellable = nullptr;
    };

} // namespace OfficeInstaller
