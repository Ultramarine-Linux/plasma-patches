// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QProcessEnvironment>
#include <QVariantMap>

#include <optional>

namespace DgpuLaunch {

    /**
     * the environment that runs an app on the discrete GPU, from
     * switcheroo-control's GPUs property (Name, Environment, Default, Discrete).
     * Picks the first discrete GPU that isn't the default, then any GPU that
     * isn't the default, like KIO does for PrefersNonDefaultGPU. Empty if the
     * default GPU is the only one worth using.
     */
    QProcessEnvironment gpuEnvironment(const QList<QVariantMap> &gpus);

    /// switcheroo-control's GPUs, or nothing if switcheroo-control isn't running
    std::optional<QList<QVariantMap>> switcherooGpus();

    /**
     * for when switcheroo-control isn't running. With the NVIDIA driver loaded
     * (@p nvidiaLoaded), the PRIME render offload variables switcheroo-control
     * sets for NVIDIA GPUs. Otherwise DRI_PRIME=1, which Mesa reads as "a GPU
     * other than the default".
     */
    QProcessEnvironment fallbackEnvironment(bool nvidiaLoaded);

    /// the discrete GPU's environment from switcheroo-control, or fallbackEnvironment() without it
    QProcessEnvironment discreteGpuEnvironment();

} // namespace DgpuLaunch
