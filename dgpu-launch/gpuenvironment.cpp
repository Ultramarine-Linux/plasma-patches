// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "gpuenvironment.h"

#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFileInfo>

#include <algorithm>
#include <functional>
#include <optional>

using namespace Qt::StringLiterals;

namespace DgpuLaunch {

    namespace {
        QProcessEnvironment environmentFromNameValuePairs(const QStringList &pairs) {
            QProcessEnvironment environment;
            for (qsizetype i = 0; i + 1 < pairs.size(); i += 2) {
                environment.insert(pairs.at(i), pairs.at(i + 1));
            }
            return environment;
        }

        std::optional<QList<QVariantMap>> switcherooGpus() {
            QDBusMessage call =
                QDBusMessage::createMethodCall(u"net.hadess.SwitcherooControl"_s, u"/net/hadess/SwitcherooControl"_s,
                                               u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
            call << u"net.hadess.SwitcherooControl"_s << u"GPUs"_s;
            const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call);
            if (!reply.isValid()) {
                return std::nullopt;
            }
            QList<QVariantMap> gpus;
            qvariant_cast<QDBusArgument>(reply.value().variant()) >> gpus;
            return gpus;
        }
    } // namespace

    QProcessEnvironment gpuEnvironment(const QList<QVariantMap> &gpus) {
        const auto isDefault = [](const QVariantMap &gpu) { return gpu.value(u"Default"_s).toBool(); };
        const auto isDiscrete = [](const QVariantMap &gpu) { return gpu.value(u"Discrete"_s).toBool(); };
        auto chosen =
            std::ranges::find_if(gpus, [&](const QVariantMap &gpu) { return isDiscrete(gpu) && !isDefault(gpu); });
        if (chosen == gpus.end()) {
            chosen = std::ranges::find_if(gpus, std::not_fn(isDefault));
        }
        if (chosen == gpus.end()) {
            return {};
        }
        return environmentFromNameValuePairs(chosen->value(u"Environment"_s).toStringList());
    }

    QProcessEnvironment fallbackEnvironment(const bool nvidiaLoaded) {
        QProcessEnvironment environment;
        if (nvidiaLoaded) {
            environment.insert(u"__GLX_VENDOR_LIBRARY_NAME"_s, u"nvidia"_s);
            environment.insert(u"__NV_PRIME_RENDER_OFFLOAD"_s, u"1"_s);
            environment.insert(u"__VK_LAYER_NV_optimus"_s, u"NVIDIA_only"_s);
        } else {
            environment.insert(u"DRI_PRIME"_s, u"1"_s);
        }
        return environment;
    }

    QProcessEnvironment discreteGpuEnvironment() {
        if (const auto gpus = switcherooGpus()) {
            return gpuEnvironment(*gpus);
        }
        return fallbackEnvironment(QFileInfo::exists(u"/sys/module/nvidia"_s));
    }

} // namespace DgpuLaunch
