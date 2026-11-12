// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "gpuenvironment.h"

using namespace Qt::StringLiterals;
using namespace DgpuLaunch;

namespace {
    QVariantMap gpu(const QString &driPrime, const bool isDefault, const bool discrete) {
        return {{u"Name"_s, driPrime},
                {u"Environment"_s, QStringList{u"DRI_PRIME"_s, driPrime, u"VK_LOADER_DRIVERS_SELECT"_s, u"*"_s}},
                {u"Default"_s, isDefault},
                {u"Discrete"_s, discrete}};
    }

    QString driPrime(const QList<QVariantMap> &gpus) { return gpuEnvironment(gpus).value(u"DRI_PRIME"_s); }

    class DgpuLaunchTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void picksTheDiscreteGpu();
        static void fallsBackToANonDefaultGpu();
        static void leavesSingleGpusAlone();
        static void readsEveryVariable();
        static void fallsBackWithoutSwitcheroo();
    };
} // namespace

void DgpuLaunchTest::picksTheDiscreteGpu() {
    QCOMPARE(driPrime({gpu(u"igpu"_s, true, false), gpu(u"dgpu"_s, false, true)}), u"dgpu"_s);
    QCOMPARE(driPrime({gpu(u"dgpu"_s, false, true), gpu(u"igpu"_s, true, false)}), u"dgpu"_s);
    QCOMPARE(driPrime({gpu(u"igpu"_s, true, false), gpu(u"dgpu"_s, false, true), gpu(u"egpu"_s, false, true)}),
             u"dgpu"_s);
}

void DgpuLaunchTest::fallsBackToANonDefaultGpu() {
    QCOMPARE(driPrime({gpu(u"igpu"_s, true, false), gpu(u"other"_s, false, false)}), u"other"_s);
    QCOMPARE(driPrime({gpu(u"dgpu"_s, true, true), gpu(u"igpu"_s, false, false)}), u"igpu"_s);
}

void DgpuLaunchTest::leavesSingleGpusAlone() {
    QVERIFY(gpuEnvironment({gpu(u"igpu"_s, true, false)}).isEmpty());
    QVERIFY(gpuEnvironment({gpu(u"dgpu"_s, true, true)}).isEmpty());
    QVERIFY(gpuEnvironment({}).isEmpty());
}

void DgpuLaunchTest::readsEveryVariable() {
    QVariantMap nvidia = gpu(u"unused"_s, false, true);
    nvidia[u"Environment"_s] = QStringList{u"__NV_PRIME_RENDER_OFFLOAD"_s,
                                           u"1"_s,
                                           u"__GLX_VENDOR_LIBRARY_NAME"_s,
                                           u"nvidia"_s,
                                           u"__VK_LAYER_NV_optimus"_s,
                                           u"NVIDIA_only"_s,
                                           u"DANGLING"_s};
    const QProcessEnvironment environment = gpuEnvironment({gpu(u"igpu"_s, true, false), nvidia});
    QCOMPARE(environment.keys().size(), 3);
    QCOMPARE(environment.value(u"__NV_PRIME_RENDER_OFFLOAD"_s), u"1"_s);
    QCOMPARE(environment.value(u"__GLX_VENDOR_LIBRARY_NAME"_s), u"nvidia"_s);
    QCOMPARE(environment.value(u"__VK_LAYER_NV_optimus"_s), u"NVIDIA_only"_s);
}

void DgpuLaunchTest::fallsBackWithoutSwitcheroo() {
    const QProcessEnvironment mesa = fallbackEnvironment(false);
    QCOMPARE(mesa.keys(), QStringList{u"DRI_PRIME"_s});
    QCOMPARE(mesa.value(u"DRI_PRIME"_s), u"1"_s);

    // the proprietary driver ignores DRI_PRIME
    const QProcessEnvironment nvidia = fallbackEnvironment(true);
    QCOMPARE(nvidia.keys().size(), 3);
    QVERIFY(!nvidia.contains(u"DRI_PRIME"_s));
    QCOMPARE(nvidia.value(u"__GLX_VENDOR_LIBRARY_NAME"_s), u"nvidia"_s);
    QCOMPARE(nvidia.value(u"__NV_PRIME_RENDER_OFFLOAD"_s), u"1"_s);
    QCOMPARE(nvidia.value(u"__VK_LAYER_NV_optimus"_s), u"NVIDIA_only"_s);
}

QTEST_GUILESS_MAIN(DgpuLaunchTest)

#include "test_dgpu_launch.moc"
