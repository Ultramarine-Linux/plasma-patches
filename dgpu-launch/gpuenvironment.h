// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QProcessEnvironment>
#include <QVariantMap>

#include <optional>

namespace DgpuLaunch {

    QProcessEnvironment gpuEnvironment(const QList<QVariantMap> &gpus);

    std::optional<QList<QVariantMap>> switcherooGpus();

    QProcessEnvironment fallbackEnvironment(bool nvidiaLoaded);

    QProcessEnvironment discreteGpuEnvironment();

} // namespace DgpuLaunch
