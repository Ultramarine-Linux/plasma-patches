// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>

namespace OfficeInstaller {

    // the CLIs can't be used from a GUI. Their -y flag also marks the transaction
    // non-interactive, so polkit refuses non-admin users instead of asking for a password
    class Installer : public QObject {
        Q_OBJECT

    public:
        using QObject::QObject;

        virtual void start() = 0;
        virtual void cancel() = 0;

    Q_SIGNALS:
        void progress(const QString &text);
        void finished(bool success, bool canceled, const QString &error);
    };

} // namespace OfficeInstaller
