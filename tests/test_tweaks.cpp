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

namespace {
    TweakItem tweakWithId(const QString &id) {
        TweakItem item;
        item.id = id;
        return item;
    }

    /// returns whether process @p pid is gone, counting a zombie as gone
    bool processGone(const qint64 pid) {
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) {
            return true;
        }
        // the state comes after the command name, which is in parentheses
        const QByteArray line = stat.readAll();
        const qsizetype end = line.lastIndexOf(')');
        return end < 0 || line.mid(end + 2, 1) == "Z";
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
        /// creates the module and waits for its first list
        std::unique_ptr<Tweaks> loadedTweaks();

        std::unique_ptr<QTemporaryDir> m_dir;
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

void TweaksTest::rejectsUnrecognizedVersions() {
    // anything but a positive whole number isn't read as version 1
    for (const char *version: {R"("2")", "2.5", "null", "0", "-1", "true", "{}"}) {
        const QString output = QStringLiteral(R"({"version":%1,"tweaks":[]})").arg(QLatin1String(version));
        QCOMPARE(tweaksOutputVersion(output), UnrecognizedTweaksOutputVersion);
        QVERIFY(!parseTweaksOutput(output, nullptr));
    }
    QCOMPARE(tweaksOutputVersion(QStringLiteral(R"({"version":2.0,"tweaks":[]})")), 2);
    QCOMPARE(tweaksOutputVersion(QStringLiteral(R"({"version":1e20,"tweaks":[]})")), UnrecognizedTweaksOutputVersion);
    // a missing version is version 1
    QCOMPARE(tweaksOutputVersion(QStringLiteral(R"({"tweaks":[]})")), 1);
    QVERIFY(parseTweaksOutput(QStringLiteral(R"({"tweaks":[]})"), nullptr));
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
    QVERIFY(tweaks->tweaks()->find(QStringLiteral("nix")));
}

void TweaksTest::listFailures_data() {
    QTest::addColumn<QByteArray>("mode");
    QTest::addColumn<QString>("error");
    QTest::newRow("too old") << QByteArray("too-old")
                             << QStringLiteral("This version of um is too old to manage tweaks from System Settings.");
    QTest::newRow("too new") << QByteArray("too-new")
                             << QStringLiteral("This version of um is too new for this version of System Settings. "
                                               "Update System Settings to manage tweaks.");
    QTest::newRow("unrecognized version")
        << QByteArray("unrecognized")
        << QStringLiteral("um listed the tweaks with a format version System Settings doesn't recognize. Check that "
                          "um and System Settings are up to date.");
    // without the timestamp Go's log package adds
    QTest::newRow("um error") << QByteArray("error")
                              << QStringLiteral("Could not load the list of tweaks: open /usr/share/um/tweaks: no "
                                                "such file or directory");
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
    tweaks->setTweakEnabled(QStringLiteral("nix"), true);
    QVERIFY(tweaks->running());
    QVERIFY(tweaks->tweaks()->data(tweaks->tweaks()->index(0, 0), TweaksModel::BusyRole).toBool());
    QTRY_VERIFY(!tweaks->running());
    // the list is loaded again afterwards
    QTRY_VERIFY(!tweaks->loading());

    QCOMPARE(running.count(), 2);
    QVERIFY(tweaks->errorMessage().isEmpty());
    QCOMPARE(tweaks->output(), QStringLiteral("TASK [Install Nix] *****\nok: [localhost]\n"));
    QFile args(QDir(m_dir->path()).filePath(QStringLiteral("change.args")));
    QVERIFY(args.open(QIODevice::ReadOnly));
    QCOMPARE(args.readAll().trimmed(), QByteArray("tweaks enable --yes nix"));
    // the output file is unlinked once um has started
    QVERIFY(QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation))
                .entryList({QStringLiteral("kcm_ultramarine_tweaks-*.log")}, QDir::Files)
                .isEmpty());
}

void TweaksTest::changeFails() {
    qputenv("FAKE_UM_CHANGE", "fail");
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    tweaks->runTweak(QStringLiteral("cleanup"));
    QVERIFY(tweaks->running());
    QTRY_VERIFY(!tweaks->running());
    QTRY_VERIFY(!tweaks->loading());
    // stderr goes to the same file, and um's error is shown without its timestamp
    QVERIFY(tweaks->output().contains(QLatin1String("2026/11/02 13:04:05 tweak is locked")));
    QCOMPARE(tweaks->errorMessage(), QStringLiteral("The maintenance task failed: tweak is locked"));
    // a change's error stays after the list loads again
    QVERIFY(tweaks->loaded());
    QVERIFY(!tweaks->listFailed());
}

void TweaksTest::showsChangeProgress() {
    qputenv("FAKE_UM_CHANGE", "slow");
    const auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    tweaks->setTweakEnabled(QStringLiteral("nix"), true);
    // read from the output file while um runs, without the asterisks
    QTRY_COMPARE(tweaks->status(), QStringLiteral("TASK [first step]"));
    QVERIFY(tweaks->running());
    QTRY_VERIFY_WITH_TIMEOUT(!tweaks->running(), 10000);
    QVERIFY(tweaks->status().isEmpty());
    QVERIFY(tweaks->output().endsWith(QLatin1String("TASK [second step] *****\n")));
}

void TweaksTest::destroyingWhileListingIsSafe() {
    // ~QProcess used to emit finished() for the running list into the half-destroyed module,
    // which then wrote to its already destroyed members
    qputenv("FAKE_UM_LIST", "hang");
    auto tweaks = std::make_unique<Tweaks>(nullptr, KPluginMetaData());
    tweaks->load();
    QVERIFY(tweaks->loading());
    QFile pidFile(QDir(m_dir->path()).filePath(QStringLiteral("check.pid")));
    QTRY_VERIFY(pidFile.exists() && pidFile.size() > 0);
    QVERIFY(pidFile.open(QIODevice::ReadOnly));
    const qint64 checkPid = pidFile.readAll().trimmed().toLongLong();
    QVERIFY(checkPid > 0);
    QVERIFY(!processGone(checkPid));

    const QSignalSpy loadingChanged(tweaks.get(), &Tweaks::loadingChanged);
    const QSignalSpy errorMessageChanged(tweaks.get(), &Tweaks::errorMessageChanged);
    tweaks.reset();
    QCOMPARE(loadingChanged.count(), 0);
    QCOMPARE(errorMessageChanged.count(), 0);
    // um's children (its check.sh scripts) are stopped with it, not left behind
    QTRY_VERIFY(processGone(checkPid));
}

void TweaksTest::destroyingWhileChangingLetsItFinish() {
    qputenv("FAKE_UM_CHANGE", "slow");
    auto tweaks = loadedTweaks();
    QVERIFY(tweaks);
    tweaks->setTweakEnabled(QStringLiteral("nix"), true);
    QTRY_VERIFY(!tweaks->status().isEmpty() && tweaks->status() != QStringLiteral("Enabling “Install Nix”…"));

    // ~Tweaks neither kills um nor waits for it
    QElapsedTimer timer;
    timer.start();
    tweaks.reset();
    QVERIFY(timer.elapsed() < 500);
    const QString done = QDir(m_dir->path()).filePath(QStringLiteral("change.done"));
    QVERIFY(!QFile::exists(done));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(done), 10000);
}

QTEST_GUILESS_MAIN(TweaksTest)

#include "test_tweaks.moc"
