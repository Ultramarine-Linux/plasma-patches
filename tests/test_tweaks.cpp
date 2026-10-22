// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTest>

#include "tweaks.h"
#include "umcli.h"

namespace {
    TweakItem tweakWithId(const QString &id) {
        TweakItem item;
        item.id = id;
        return item;
    }
} // namespace

namespace {
    class TweaksTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void parsesAndGroupsTweaks();
        static void rejectsInvalidOutput();
        static void modelExposesRoles();
        static void modelTracksBusyTweak();
        static void authenticationExitCodes();
    };
} // namespace

void TweaksTest::parsesAndGroupsTweaks() {
    QList<TweakItem> tweaks;
    QVERIFY(parseTweaksOutput(QStringLiteral(R"({"tweaks":[
        {"id":"cleanup","description":"Remove old kernels.","type":"script","stability":"stable","enabled":false},
        {"id":"zz","description":"Zebra","type":"toggle","enabled":true},
        {"id":"nix","description":"Install Nix","type":"toggle","stability":"alpha","warning":"careful","enabled":false},
        {"id":"once","type":"oneshot","enabled":true},
        {"description":"missing id is skipped","type":"toggle"}
    ]})"),
                              &tweaks));

    QCOMPARE(tweaks.size(), 4);
    QCOMPARE(tweaks.at(0).id, QStringLiteral("nix"));
    QCOMPARE(tweaks.at(0).stability, QStringLiteral("alpha"));
    QCOMPARE(tweaks.at(0).warning, QStringLiteral("careful"));
    QCOMPARE(tweaks.at(1).id, QStringLiteral("zz"));
    QVERIFY(tweaks.at(1).enabled);
    QCOMPARE(tweaks.at(2).id, QStringLiteral("once"));
    // A missing description falls back to the ID.
    QCOMPARE(tweaks.at(2).description, QStringLiteral("once"));
    QCOMPARE(tweaks.at(3).type, QStringLiteral("script"));
}

void TweaksTest::rejectsInvalidOutput() {
    QList tweaks{tweakWithId(QStringLiteral("keep"))};
    QVERIFY(!parseTweaksOutput(QStringLiteral("- ID: nix"), &tweaks));
    QVERIFY(!parseTweaksOutput(QStringLiteral(R"({"updates":[]})"), &tweaks));
    QCOMPARE(tweaks.size(), 1);
    QVERIFY(parseTweaksOutput(QStringLiteral(R"({"tweaks":[]})"), &tweaks));
    QVERIFY(tweaks.isEmpty());
}

void TweaksTest::modelExposesRoles() {
    TweaksModel model;
    const QSignalSpy countChanged(&model, &TweaksModel::countChanged);
    model.setTweaks({{.id = QStringLiteral("nix"),
                      .description = QStringLiteral("Install Nix"),
                      .type = QStringLiteral("toggle"),
                      .stability = QStringLiteral("alpha"),
                      .warning = QStringLiteral("careful"),
                      .enabled = true}});

    QCOMPARE(model.count(), 1);
    QCOMPARE(countChanged.count(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, TweaksModel::IdRole).toString(), QStringLiteral("nix"));
    QCOMPARE(model.data(index, TweaksModel::DescriptionRole).toString(), QStringLiteral("Install Nix"));
    QCOMPARE(model.data(index, TweaksModel::TypeRole).toString(), QStringLiteral("toggle"));
    QCOMPARE(model.data(index, TweaksModel::StabilityRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(model.data(index, TweaksModel::WarningRole).toString(), QStringLiteral("careful"));
    QVERIFY(model.data(index, TweaksModel::EnabledRole).toBool());
    QVERIFY(!model.data(index, TweaksModel::BusyRole).toBool());
    QVERIFY(model.find(QStringLiteral("nix")));
    QVERIFY(!model.find(QStringLiteral("missing")));
    QVERIFY(model.roleNames().values().contains("tweakEnabled"));
}

void TweaksTest::modelTracksBusyTweak() {
    TweaksModel model;
    model.setTweaks({tweakWithId(QStringLiteral("a")), tweakWithId(QStringLiteral("b"))});
    QSignalSpy dataChanged(&model, &TweaksModel::dataChanged);

    model.setBusyId(QStringLiteral("b"));
    QCOMPARE(dataChanged.count(), 1);
    QVERIFY(!model.data(model.index(0, 0), TweaksModel::BusyRole).toBool());
    QVERIFY(model.data(model.index(1, 0), TweaksModel::BusyRole).toBool());

    model.setBusyId(QStringLiteral("b"));
    QCOMPARE(dataChanged.count(), 1);
    model.setBusyId({});
    QVERIFY(!model.data(model.index(1, 0), TweaksModel::BusyRole).toBool());
}

void TweaksTest::authenticationExitCodes() {
    QVERIFY(UmCli::isAuthenticationFailure(126));
    QVERIFY(UmCli::isAuthenticationFailure(127));
    QVERIFY(!UmCli::isAuthenticationFailure(1));
}

QTEST_MAIN(TweaksTest)

#include "test_tweaks.moc"
