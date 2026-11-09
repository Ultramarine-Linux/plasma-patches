// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <KPluginMetaData>

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
        static void checksOutputVersion();
        static void modelExposesRoles();
        static void modelTracksBusyTweak();
        static void authenticationExitCodes();
        static void parsesStateErrors();
        static void stripsGoLogTimestamps();
        static void reportsChangeErrors();
        static void composesErrors();
        static void destroyingWhileListingIsSafe();
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
    // a missing description falls back to the ID
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

void TweaksTest::checksOutputVersion() {
    QCOMPARE(tweaksOutputVersion(QStringLiteral(R"({"version":1,"tweaks":[]})")), 1);
    // um builds from before the version field print version 1's format
    QCOMPARE(tweaksOutputVersion(QStringLiteral(R"({"tweaks":[]})")), 1);
    QCOMPARE(tweaksOutputVersion(QStringLiteral("- ID: nix")), 0);

    QList tweaks{tweakWithId(QStringLiteral("keep"))};
    QVERIFY(parseTweaksOutput(QStringLiteral(R"({"version":1,"tweaks":[{"id":"nix","type":"toggle"}]})"), &tweaks));
    QCOMPARE(tweaks.size(), 1);
    QCOMPARE(tweaks.at(0).id, QStringLiteral("nix"));
    QVERIFY(!parseTweaksOutput(QStringLiteral(R"({"version":2,"tweaks":[{"id":"new","type":"toggle"}]})"), &tweaks));
    QCOMPARE(tweaks.at(0).id, QStringLiteral("nix"));
}

void TweaksTest::modelExposesRoles() {
    TweaksModel model;
    const QSignalSpy countChanged(&model, &TweaksModel::countChanged);
    model.setTweaks({{.id = QStringLiteral("nix"),
                      .description = QStringLiteral("Install Nix"),
                      .type = QStringLiteral("toggle"),
                      .stability = QStringLiteral("alpha"),
                      .warning = QStringLiteral("careful"),
                      .stateError = {},
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

void TweaksTest::parsesStateErrors() {
    QList<TweakItem> tweaks;
    QVERIFY(parseTweaksOutput(QStringLiteral(R"({"tweaks":[
        {"id":"broken","type":"toggle","enabled":true,"state_error":"check script for broken timed out"},
        {"id":"fine","type":"toggle","enabled":true}
    ]})"),
                              &tweaks));
    QCOMPARE(tweaks.size(), 2);
    QCOMPARE(tweaks.at(0).id, QStringLiteral("broken"));
    QCOMPARE(tweaks.at(0).stateError, QStringLiteral("check script for broken timed out"));
    // enabled isn't trusted when the state couldn't be read
    QVERIFY(!tweaks.at(0).enabled);
    QVERIFY(tweaks.at(1).stateError.isEmpty());
    QVERIFY(tweaks.at(1).enabled);

    TweaksModel model;
    model.setTweaks(tweaks);
    QCOMPARE(model.data(model.index(0, 0), TweaksModel::StateErrorRole).toString(),
             QStringLiteral("check script for broken timed out"));
    QVERIFY(model.roleNames().values().contains("stateError"));
}

void TweaksTest::stripsGoLogTimestamps() {
    QCOMPARE(stripGoLogTimestamp(QStringLiteral("2026/11/02 13:04:05 tweak nix not found")),
             QStringLiteral("tweak nix not found"));
    QCOMPARE(stripGoLogTimestamp(QStringLiteral("tweak nix not found")), QStringLiteral("tweak nix not found"));
    QCOMPARE(stripGoLogTimestamp(QStringLiteral("x 2026/11/02 13:04:05 y")), QStringLiteral("x 2026/11/02 13:04:05 y"));
}

void TweaksTest::reportsChangeErrors() {
    QVERIFY(changeErrorMessage(false, 0, QProcess::NormalExit, {}).isEmpty());
    QVERIFY(changeErrorMessage(true, 0, QProcess::NormalExit, {}).isEmpty());

    // um maps a playbook's or script's own 126 and 127 to 1, so these are always pkexec's
    QVERIFY(changeErrorMessage(false, 126, QProcess::NormalExit, {}).contains(QLatin1String("No changes were made")));
    QVERIFY(changeErrorMessage(true, 127, QProcess::NormalExit, {}).contains(QLatin1String("No changes were made")));

    // a crash has no meaningful exit code
    const QString crashed = changeErrorMessage(false, 9, QProcess::CrashExit, {});
    QVERIFY(!crashed.isEmpty());
    QVERIFY(!crashed.contains(QLatin1String("9")));

    // um's own error is shown without its timestamp
    QCOMPARE(changeErrorMessage(false, 1, QProcess::NormalExit,
                                QStringLiteral("TASK [x]\n2026/11/02 13:04:05 tweak is locked\n")),
             QStringLiteral("The tweak could not be changed: tweak is locked"));
    QVERIFY(changeErrorMessage(false, 2, QProcess::NormalExit, QStringLiteral("fatal: [localhost]"))
                .contains(QLatin1String("exit code 2")));
}

void TweaksTest::composesErrors() {
    QVERIFY(composeTweaksError({}, {}).isEmpty());
    QCOMPARE(composeTweaksError(QStringLiteral("change"), {}), QStringLiteral("change"));
    QCOMPARE(composeTweaksError({}, QStringLiteral("list")), QStringLiteral("list"));
    QCOMPARE(composeTweaksError(QStringLiteral("change"), QStringLiteral("list")), QStringLiteral("change\nlist"));
}

void TweaksTest::destroyingWhileListingIsSafe() {
    // ~QProcess used to emit finished() for the running list into the half-destroyed module,
    // which then wrote to its already destroyed members
    auto *tweaks = new Tweaks(nullptr, KPluginMetaData());
    tweaks->load();
    if (!tweaks->loading()) {
        delete tweaks;
        QSKIP("um isn't installed, so there's no list to interrupt");
    }
    const QSignalSpy loadingChanged(tweaks, &Tweaks::loadingChanged);
    const QSignalSpy errorMessageChanged(tweaks, &Tweaks::errorMessageChanged);
    delete tweaks;
    QCOMPARE(loadingChanged.count(), 0);
    QCOMPARE(errorMessageChanged.count(), 0);
}

QTEST_GUILESS_MAIN(TweaksTest)

#include "test_tweaks.moc"
