// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QProcessEnvironment>
#include <QVariantMap>

namespace DgpuLaunch {

    QProcessEnvironment gpuEnvironment(const QList<QVariantMap> &gpus);

    QProcessEnvironment fallbackEnvironment(bool nvidiaLoaded);

    QProcessEnvironment discreteGpuEnvironment();

} // namespace DgpuLaunch
