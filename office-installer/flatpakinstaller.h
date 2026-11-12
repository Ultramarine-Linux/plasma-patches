// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QString>

#include "installer.h"

class QThread;
// gio.h can't be included here because it clashes with Qt's signals keyword
using GCancellable = struct _GCancellable; // NOLINT(bugprone-reserved-identifier)

namespace OfficeInstaller {

    class FlatpakInstaller : public Installer {
        Q_OBJECT

    public:
        FlatpakInstaller(const QString &remote, const QString &fullRef, QObject *parent = nullptr);
        ~FlatpakInstaller() override;

        FlatpakInstaller(const FlatpakInstaller &) = delete;
        FlatpakInstaller &operator=(const FlatpakInstaller &) = delete;
        FlatpakInstaller(FlatpakInstaller &&) = delete;
        FlatpakInstaller &operator=(FlatpakInstaller &&) = delete;

        void start() override;
        void cancel() override;

        static QString defaultArch();

    private:
        QString m_remote;
        QString m_ref;
        QThread *m_thread = nullptr;
        GCancellable *m_cancellable = nullptr;
    };

    class AppstreamUpdater : public QObject {
        Q_OBJECT

    public:
        explicit AppstreamUpdater(const QString &remote, QObject *parent = nullptr);
        ~AppstreamUpdater() override;

        AppstreamUpdater(const AppstreamUpdater &) = delete;
        AppstreamUpdater &operator=(const AppstreamUpdater &) = delete;
        AppstreamUpdater(AppstreamUpdater &&) = delete;
        AppstreamUpdater &operator=(AppstreamUpdater &&) = delete;

        void start();

    Q_SIGNALS:
        void finished(bool success);

    private:
        QString m_remote;
        QThread *m_thread = nullptr;
        GCancellable *m_cancellable = nullptr;
    };

} // namespace OfficeInstaller
