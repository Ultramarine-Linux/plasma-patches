// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QString>

#include <functional>

#include "installer.h"

class QThread;
// gio.h can't be included here because it clashes with Qt's signals keyword
using GCancellable = struct _GCancellable; // NOLINT(bugprone-reserved-identifier)

namespace OfficeInstaller {

    class CancellableThread {
    public:
        CancellableThread() = default;
        ~CancellableThread();

        CancellableThread(const CancellableThread &) = delete;
        CancellableThread &operator=(const CancellableThread &) = delete;
        CancellableThread(CancellableThread &&) = delete;
        CancellableThread &operator=(CancellableThread &&) = delete;

        void start(QObject *context, const std::function<void(GCancellable *)> &work,
                   const std::function<void()> &finished);
        void cancel() const;

    private:
        QThread *m_thread = nullptr;
        GCancellable *m_cancellable = nullptr;
    };

    class FlatpakInstaller : public Installer {
        Q_OBJECT

    public:
        FlatpakInstaller(const QString &remote, const QString &fullRef, QObject *parent = nullptr);

        void start() override;
        void cancel() override;

        static QString defaultArch();

    private:
        QString m_remote;
        QString m_ref;
        CancellableThread m_thread;
    };

    class AppstreamUpdater : public QObject {
        Q_OBJECT

    public:
        explicit AppstreamUpdater(const QString &remote, QObject *parent = nullptr);

        void start();

    Q_SIGNALS:
        void finished(bool success);

    private:
        QString m_remote;
        CancellableThread m_thread;
    };

} // namespace OfficeInstaller
