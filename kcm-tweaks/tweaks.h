// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

class Tweaks : public KQuickManagedConfigModule {
    Q_OBJECT

public:
    explicit Tweaks(QObject *parent, const KPluginMetaData &metaData) : KQuickManagedConfigModule(parent, metaData) {}
};
