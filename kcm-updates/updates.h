// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <KQuickManagedConfigModule>

class Updates : public KQuickManagedConfigModule {
    Q_OBJECT

public:
    explicit Updates(QObject *parent, const KPluginMetaData &metaData) : KQuickManagedConfigModule(parent, metaData) {}
};
