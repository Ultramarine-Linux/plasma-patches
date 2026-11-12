// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <memory>

#include <KPluginMetaData>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "tweaks.h"
#include "umcli.h"

using namespace Qt::StringLiterals;

namespace {
    TweakItem tweakWithId(const QString &id) {
        TweakItem item;
        item.id = id;
        return item;
    }

    bool isProcessGoneOrZombie(const qint64 pid) {
        QFile stat(u"/proc/%1/stat"_s.arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) {
            return true;
        }
        const QByteArray line = stat.readAll();
        const qsizetype commandNameEnd = line.lastIndexOf(')');
        return commandNameEnd < 0 || line.mid(commandNameEnd + 2, 1) == "Z";
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
        static void rejectsUnrecognizedVersions();
        void init();
        void cleanup();
        void listsTweaks();
        void listFailures_data();
        void listFailures();
        void changeSucceeds();
        void changeFails();
        void showsChangeProgress();
        void destroyingWhileListingIsSafe();
        void destroyingWhileChangingLetsItFinish();

    private:
        std::unique_ptr<Tweaks> loadedTweaks();

        std::unique_ptr<QTemporaryDir> m_dir;
    };
} // namespace

void TweaksTest::parsesAndGroupsTweaks() {
    QList<TweakItem> tweaks;
    QVERIFY(parseTweaksOutput(uR"({"tweaks":[
        {"id":"cleanup","description":"Remove old kernels.","type":"script","stability":"stable","enabled":false},
        {"id":"zz","description":"Zebra","type":"toggle","enabled":true},
        {"id":"nix","description":"Install Nix","type":"toggle","stability":"alpha","warning":"careful","enabled":false},
        {"id":"once","type":"oneshot","enabled":true},
        {"description":"missing id is skipped","type":"toggle"}
    ]})"_s,
                              &tweaks));

    QCOMPARE(tweaks.size(), 4);
    QCOMPARE(tweaks.at(0).id, u"nix"_s);
    QCOMPARE(tweaks.at(0).stability, u"alpha"_s);
    QCOMPARE(tweaks.at(0).warning, u"careful"_s);
    QCOMPARE(tweaks.at(1).id, u"zz"_s);
    QVERIFY(tweaks.at(1).enabled);
    QCOMPARE(tweaks.at(2).id, u"once"_s);
    QCOMPARE(tweaks.at(2).description, u"once"_s);
    QCOMPARE(tweaks.at(3).type, u"script"_s);
}

void TweaksTest::rejectsInvalidOutput() {
    QList tweaks{tweakWithId(u"keep"_s)};
    QVERIFY(!parseTweaksOutput(u"- ID: nix"_s, &tweaks));
    QVERIFY(!parseTweaksOutput(uR"({"updates":[]})"_s, &tweaks));
    QCOMPARE(tweaks.size(), 1);
    QVERIFY(parseTweaksOutput(uR"({"tweaks":[]})"_s, &tweaks));
    QVERIFY(tweaks.isEmpty());
}

void TweaksTest::checksOutputVersion() {
    QCOMPARE(tweaksOutputVersion(uR"({"version":1,"tweaks":[]})"_s), 1);
    QCOMPARE(tweaksOutputVersion(uR"({"tweaks":[]})"_s), 1);
    QCOMPARE(tweaksOutputVersion(u"- ID: nix"_s), 0);

    QList tweaks{tweakWithId(u"keep"_s)};
    QVERIFY(parseTweaksOutput(uR"({"version":1,"tweaks":[{"id":"nix","type":"toggle"}]})"_s, &tweaks));
    QCOMPARE(tweaks.size(), 1);
    QCOMPARE(tweaks.at(0).id, u"nix"_s);
    QVERIFY(!parseTweaksOutput(uR"({"version":2,"tweaks":[{"id":"new","type":"toggle"}]})"_s, &tweaks));
    QCOMPARE(tweaks.at(0).id, u"nix"_s);
}

void TweaksTest::rejectsUnrecognizedVersions() {
    for (const char *version: {R"("2")", "2.5", "null", "0", "-1", "true", "{}"}) {
        const QString output = uR"({"version":%1,"tweaks":[]})"_s.arg(QLatin1StringView(version));
        QCOMPARE(tweaksOutputVersion(output), UnrecognizedTweaksOutputVersion);
        QVERIFY(!parseTweaksOutput(output, nullptr));
    }
    QCOMPARE(tweaksOutputVersion(uR"({"version":2.0,"tweaks":[]})"_s), 2);
    QCOMPARE(tweaksOutputVersion(uR"({"version":1e20,"tweaks":[]})"_s), UnrecognizedTweaksOutputVersion);
    QCOMPARE(tweaksOutputVersion(uR"({"tweaks":[]})"_s), 1);
    QVERIFY(parseTweaksOutput(uR"({"tweaks":[]})"_s, nullptr));
}

void TweaksTest::modelExposesRoles() {
    TweaksModel model;
    const QSignalSpy countChanged(&model, &TweaksModel::countChanged);
    model.setTweaks({{.id = u"nix"_s,
                      .description = u"Install Nix"_s,
                      .type = u"toggle"_s,
                      .stability = u"alpha"_s,
                      .warning = u"careful"_s,
                      .stateError = {},
                      .enabled = true}});

    QCOMPARE(model.count(), 1);
    QCOMPARE(countChanged.count(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, TweaksModel::IdRole).toString(), u"nix"_s);
    QCOMPARE(model.data(index, TweaksModel::DescriptionRole).toString(), u"Install Nix"_s);
    QCOMPARE(model.data(index, TweaksModel::TypeRole).toString(), u"toggle"_s);
    QCOMPARE(model.data(index, TweaksModel::StabilityRole).toString(), u"alpha"_s);
    QCOMPARE(model.data(index, TweaksModel::WarningRole).toString(), u"careful"_s);
    QVERIFY(model.data(index, TweaksModel::EnabledRole).toBool());
    QVERIFY(!model.data(index, TweaksModel::BusyRole).toBool());
    QVERIFY(model.find(u"nix"_s));
    QVERIFY(!model.find(u"missing"_s));
    QVERIFY(model.roleNames().values().contains("tweakEnabled"));
}

void TweaksTest::modelTracksBusyTweak() {
    TweaksModel model;
    model.setTweaks({tweakWithId(u"a"_s), tweakWithId(u"b"_s)});
    QSignalSpy dataChanged(&model, &TweaksModel::dataChanged);

    model.setBusyId(u"b"_s);
    QCOMPARE(dataChanged.count(), 1);
    QVERIFY(!model.data(model.index(0, 0), TweaksModel::BusyRole).toBool());
    QVERIFY(model.data(model.index(1, 0), TweaksModel::BusyRole).toBool());

    model.setBusyId(u"b"_s);
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
    QVERIFY(parseTweaksOutput(uR"({"tweaks":[
        {"id":"broken","type":"toggle","enabled":true,"state_error":"check script for broken timed out"},
        {"id":"fine","type":"toggle","enabled":true}
    ]})"_s,
                              &tweaks));
    QCOMPARE(tweaks.size(), 2);
    QCOMPARE(tweaks.at(0).id, u"broken"_s);
    QCOMPARE(tweaks.at(0).stateError, u"check script for broken timed out"_s);
    QVERIFY(!tweaks.at(0).enabled);
    QVERIFY(tweaks.at(1).stateError.isEmpty());
    QVERIFY(tweaks.at(1).enabled);

    TweaksModel model;
    model.setTweaks(tweaks);
    QCOMPARE(model.data(model.index(0, 0), TweaksModel::StateErrorRole).toString(),
             u"check script for broken timed out"_s);
    QVERIFY(model.roleNames().values().contains("stateError"));
}

void TweaksTest::stripsGoLogTimestamps() {
    QCOMPARE(stripGoLogTimestamp(u"2026/11/02 13:04:05 tweak nix not found"_s), u"tweak nix not found"_s);
    QCOMPARE(stripGoLogTimestamp(u"tweak nix not found"_s), u"tweak nix not found"_s);
    QCOMPARE(stripGoLogTimestamp(u"x 2026/11/02 13:04:05 y"_s), u"x 2026/11/02 13:04:05 y"_s);
}

void TweaksTest::reportsChangeErrors() {
    QVERIFY(changeErrorMessage(false, 0, QProcess::NormalExit, {}).isEmpty());
    QVERIFY(changeErrorMessage(true, 0, QProcess::NormalExit, {}).isEmpty());

    // um maps its playbooks' own 126 and 127 to 1, so these always come from pkexec
    QVERIFY(changeErrorMessage(false, 126, QProcess::NormalExit, {}).contains("No changes were made"_L1));
    QVERIFY(changeErrorMessage(true, 127, QProcess::NormalExit, {}).contains("No changes were made"_L1));

    const QString crashedWithMeaninglessCode = changeErrorMessage(false, 9, QProcess::CrashExit, {});
    QVERIFY(!crashedWithMeaninglessCode.isEmpty());
    QVERIFY(!crashedWithMeaninglessCode.contains("9"_L1));

    QCOMPARE(changeErrorMessage(false, 1, QProcess::NormalExit, u"TASK [x]\n2026/11/02 13:04:05 tweak is locked\n"_s),
             u"The tweak could not be changed: tweak is locked"_s);
    QVERIFY(changeErrorMessage(false, 2, QProcess::NormalExit, u"fatal: [localhost]"_s).contains("exit code 2"_L1));
}

void TweaksTest::composesErrors() {
    QVERIFY(composeTweaksError({}, {}).isEmpty());
    QCOMPARE(composeTweaksError(u"change"_s, {}), u"change"_s);
    QCOMPARE(composeTweaksError({}, u"list"_s), u"list"_s);
    QCOMPARE(composeTweaksError(u"change"_s, u"list"_s), u"change\nlist"_s);
}

void TweaksTest::init() {
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("FAKE_UM_DIR", QFile::encodeName(m_dir->path()));
    qputenv("FAKE_UM_LIST", "ok");
    qputenv("FAKE_UM_CHANGE", "ok");
    QCOMPARE(UmCli::executable(), QStringLiteral(UM_TEST_EXECUTABLE));
}

void TweaksTest::cleanup() { m_dir.reset(); }

std::unique_ptr<Tweaks> TweaksTest::loadedTweaks() {
    auto tweaks = std::make_unique<Tweaks>(nullptr, KPluginMetaData());
    tweaks->load();
    if (!QTest::qWaitFor([&tweaks] { return !tweaks->loading(); })) {
        return nullptr;
    }
    return tweaks;
}

void TweaksTest::listsTweaks() {
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    QVERIFY(tweaks->loaded());
    QVERIFY(!tweaks->listFailed());
    QVERIFY(tweaks->errorMessage().isEmpty());
    QCOMPARE(tweaks->tweaks()->count(), 2);
    QVERIFY(tweaks->tweaks()->find(u"nix"_s));
}

void TweaksTest::listFailures_data() {
    QTest::addColumn<QByteArray>("mode");
    QTest::addColumn<QString>("error");
    QTest::newRow("too old") << QByteArray("too-old")
                             << u"This version of um is too old to manage tweaks from System Settings."_s;
    QTest::newRow("too new") << QByteArray("too-new")
                             << u"This version of um is too new for this version of System Settings. "
                                u"Update System Settings to manage tweaks."_s;
    QTest::newRow("unrecognized version")
        << QByteArray("unrecognized")
        << u"um listed the tweaks with a format version System Settings doesn't recognize. Check that "
           u"um and System Settings are up to date."_s;
    QTest::newRow("um error") << QByteArray("error")
                              << u"Could not load the list of tweaks: open /usr/share/um/tweaks: no "
                                 u"such file or directory"_s;
}

void TweaksTest::listFailures() {
    QFETCH(QByteArray, mode);
    QFETCH(QString, error);
    qputenv("FAKE_UM_LIST", mode);
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    QVERIFY(tweaks->listFailed());
    QVERIFY(!tweaks->loaded());
    QCOMPARE(tweaks->errorMessage(), error);
    QCOMPARE(tweaks->tweaks()->count(), 0);
}

void TweaksTest::changeSucceeds() {
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    QSignalSpy running(tweaks.get(), &Tweaks::runningChanged);
    tweaks->setTweakEnabled(u"nix"_s, true);
    QVERIFY(tweaks->running());
    QVERIFY(tweaks->tweaks()->data(tweaks->tweaks()->index(0, 0), TweaksModel::BusyRole).toBool());
    QTRY_VERIFY(!tweaks->running());
    QTRY_VERIFY(!tweaks->loading());

    QCOMPARE(running.count(), 2);
    QVERIFY(tweaks->errorMessage().isEmpty());
    QCOMPARE(tweaks->output(), u"TASK [Install Nix] *****\nok: [localhost]\n"_s);
    QFile args(m_dir->filePath(u"change.args"_s));
    QVERIFY(args.open(QIODevice::ReadOnly));
    QCOMPARE(args.readAll().trimmed(), QByteArray("tweaks enable --yes nix"));
    QVERIFY(QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation))
                .entryList({u"kcm_ultramarine_tweaks-*.log"_s}, QDir::Files)
                .isEmpty());
}

void TweaksTest::changeFails() {
    qputenv("FAKE_UM_CHANGE", "fail");
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    tweaks->runTweak(u"cleanup"_s);
    QVERIFY(tweaks->running());
    QTRY_VERIFY(!tweaks->running());
    QTRY_VERIFY(!tweaks->loading());
    QVERIFY(tweaks->output().contains("2026/11/02 13:04:05 tweak is locked"_L1));
    QCOMPARE(tweaks->errorMessage(), u"The maintenance task failed: tweak is locked"_s);
    QVERIFY(tweaks->loaded());
    QVERIFY(!tweaks->listFailed());
}

void TweaksTest::showsChangeProgress() {
    qputenv("FAKE_UM_CHANGE", "slow");
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    tweaks->setTweakEnabled(u"nix"_s, true);
    QTRY_COMPARE(tweaks->status(), u"TASK [first step]"_s);
    QVERIFY(tweaks->running());
    QTRY_VERIFY_WITH_TIMEOUT(!tweaks->running(), 10000);
    QVERIFY(tweaks->status().isEmpty());
    QVERIFY(tweaks->output().endsWith("TASK [second step] *****\n"_L1));
}

void TweaksTest::destroyingWhileListingIsSafe() {
    qputenv("FAKE_UM_LIST", "hang");
    auto tweaks = std::make_unique<Tweaks>(nullptr, KPluginMetaData());
    tweaks->load();
    QVERIFY(tweaks->loading());
    QFile pidFile(m_dir->filePath(u"check.pid"_s));
    QTRY_VERIFY(pidFile.exists() && pidFile.size() > 0);
    QVERIFY(pidFile.open(QIODevice::ReadOnly));
    const qint64 umChildCheckScriptPid = pidFile.readAll().trimmed().toLongLong();
    QVERIFY(umChildCheckScriptPid > 0);
    QVERIFY(!isProcessGoneOrZombie(umChildCheckScriptPid));

    const QSignalSpy loadingChanged(tweaks.get(), &Tweaks::loadingChanged);
    const QSignalSpy errorMessageChanged(tweaks.get(), &Tweaks::errorMessageChanged);
    tweaks.reset();
    QCOMPARE(loadingChanged.count(), 0);
    QCOMPARE(errorMessageChanged.count(), 0);
    QTRY_VERIFY(isProcessGoneOrZombie(umChildCheckScriptPid));
}

void TweaksTest::destroyingWhileChangingLetsItFinish() {
    qputenv("FAKE_UM_CHANGE", "slow");
    auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    tweaks->setTweakEnabled(u"nix"_s, true);
    QTRY_VERIFY(!tweaks->status().isEmpty() && tweaks->status() != u"Enabling “Install Nix”…"_s);

    QElapsedTimer timer;
    timer.start();
    tweaks.reset();
    QVERIFY(timer.elapsed() < 500);
    const QString done = m_dir->filePath(u"change.done"_s);
    QVERIFY(!QFile::exists(done));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(done), 10000);
}

QTEST_GUILESS_MAIN(TweaksTest)

#include "test_tweaks.moc"
