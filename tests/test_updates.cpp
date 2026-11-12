// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTest>

#include "updates.h"

#include "cli.h"
#include "settings.h"

using namespace Qt::StringLiterals;

namespace {
    class UpdatesTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void modelExposesUpdateRoles();
        static void modelClearOnlyNotifiesWhenNeeded();
        static void modelCountsExcludingBackend();
        static void parsesUpdateListing();
        static void rejectsInvalidUpdateListing();
        static void parsesListingErrors();
        static void listsAppsFirst();
        static void parsesRestartState();
        static void parsesOfflinePending();
        static void refreshErrorMessages();
        static void upgradeSettingsDefaults();
        static void parsesUpgradeSettings();
        static void parsesScheduleStatus();
        static void upgradeSettingsIgnoreUnusedWindow();
        static void helperAcceptsUpgradeSettingsArguments();
        static void installErrorMessages();
        static void previewModelLimitsRows();
        static void composesErrorMessages();
        static void findsLastOutputLine();
    };
} // namespace

void UpdatesTest::modelExposesUpdateRoles() {
    UpdatesModel model;
    const QSignalSpy countChanged(&model, &UpdatesModel::countChanged);

    UpdateItem item;
    item.name = u"firefox"_s;
    item.source = u"fedora"_s;
    item.oldVersion = u"1"_s;
    item.newVersion = u"2"_s;
    item.icon = u"firefox"_s;
    item.kind = u"app"_s;
    item.backend = u"rpm"_s;
    item.appId = u"org.mozilla.firefox"_s;
    model.addUpdate(item);

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.rowCount({}), 1);
    QCOMPARE(countChanged.count(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, UpdatesModel::NameRole).toString(), u"firefox"_s);
    QCOMPARE(model.data(index, UpdatesModel::SourceRole).toString(), u"fedora"_s);
    QCOMPARE(model.data(index, UpdatesModel::OldVersionRole).toString(), u"1"_s);
    QCOMPARE(model.data(index, UpdatesModel::NewVersionRole).toString(), u"2"_s);
    QCOMPARE(model.data(index, UpdatesModel::IconRole).toString(), u"firefox"_s);
    QCOMPARE(model.data(index, UpdatesModel::KindRole).toString(), u"app"_s);
    QCOMPARE(model.data(index, UpdatesModel::AppIdRole).toString(), u"org.mozilla.firefox"_s);
    QCOMPARE(model.data(index, UpdatesModel::SourceLabelRole).toString(), u"System"_s);
    QVERIFY(!model.data({}, UpdatesModel::NameRole).isValid());
}

void UpdatesTest::modelClearOnlyNotifiesWhenNeeded() {
    UpdatesModel model;
    const QSignalSpy countChanged(&model, &UpdatesModel::countChanged);

    model.clear();
    QCOMPARE(countChanged.count(), 0);
    model.addUpdate({});
    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(countChanged.count(), 2);
}

void UpdatesTest::modelCountsExcludingBackend() {
    UpdatesModel model;
    QCOMPARE(model.countExcluding(u"rpm"_s), 0);

    for (const QString &backend: {u"rpm"_s, u"flatpak"_s, u"plasmoid"_s}) {
        UpdateItem item;
        item.backend = backend;
        model.addUpdate(item);
    }
    QCOMPARE(model.countExcluding(u"rpm"_s), 2);
    QCOMPARE(model.countExcluding(u"nix"_s), 3);
}

void UpdatesTest::parsesUpdateListing() {
    QString technical;
    const auto updates = parseUpdateListing(
        uR"({"updates":[{"name":"foo","source":"fedora","old_version":"1.0","new_version":"2.0"},{"name":"bar","source":"flathub","old_version":"3","new_version":"4","icon":"bar-icon"}]})"_s,
        &technical);

    QCOMPARE(updates.size(), 2);
    QCOMPARE(updates.at(0).icon, u"package-x-generic"_s);
    QCOMPARE(updates.at(1).icon, u"bar-icon"_s);
    QCOMPARE(technical, u"foo: 1.0 → 2.0 (fedora)\nbar: 3 → 4 (flathub)\n"_s);
}

void UpdatesTest::rejectsInvalidUpdateListing() {
    auto technical = u"unchanged"_s;
    QVERIFY(parseUpdateListing(u"not json"_s, &technical).isEmpty());
    QCOMPARE(technical, u"unchanged"_s);
}

void UpdatesTest::parsesListingErrors() {
    QStringList errors{u"stale"_s};
    const auto updates = parseUpdateListing(
        uR"json({"updates":[{"id":"foo.x86_64","name":"foo","source":"RPM (fedora)","old_version":"1","new_version":"2"}],"errors":["flatpak (user): failed"]})json"_s,
        nullptr, &errors);
    QCOMPARE(updates.size(), 1);
    QCOMPARE(errors, QStringList{u"flatpak (user): failed"_s});

    parseUpdateListing(uR"({"updates":[],"errors":[]})"_s, nullptr, &errors);
    QVERIFY(errors.isEmpty());
}

void UpdatesTest::listsAppsFirst() {
    const auto updates = parseUpdateListing(uR"json({"updates":[
        {"id":"glibc.x86_64","name":"glibc","source":"RPM (updates)","backend":"rpm","kind":"package","icon":"package-x-generic"},
        {"id":"org.gnome.Platform//51","name":"GNOME Platform","source":"Flatpak (flathub)","backend":"flatpak","kind":"runtime","icon":"org.gnome.Platform"},
        {"id":"firefox.x86_64","name":"Firefox","source":"RPM (updates)","backend":"rpm","kind":"app","icon":"firefox","app_id":"org.mozilla.firefox"},
        {"id":"bash.x86_64","name":"bash","source":"RPM (updates)","backend":"rpm","kind":"package"},
        {"id":"org.kde.haruna//stable","name":"Haruna","source":"Flatpak (flathub)","backend":"flatpak","kind":"app","icon":"org.kde.haruna","app_id":"org.kde.haruna"},
        {"id":"nix:flox","name":"flox","source":"Nix (github:flox/flox/latest)","backend":"nix","kind":"package","icon":"package-x-generic"},
        {"id":"plasmoid:2130967","name":"Panel Colorizer","source":"KDE Store","backend":"plasmoid","kind":"addon","icon":"desktop"}
    ]})json"_s);

    QStringList names;
    for (const UpdateItem &item: updates) {
        names << item.name;
    }
    QCOMPARE(names,
             QStringList({u"Firefox"_s, u"Haruna"_s, u"Panel Colorizer"_s, u"glibc"_s, u"bash"_s, u"flox"_s,
                          u"GNOME Platform"_s}));
    QCOMPARE(updates.at(0).appId, u"org.mozilla.firefox"_s);
    QCOMPARE(updateSourceLabel(updates.at(1)), u"Flatpak"_s);
    QCOMPARE(updateSourceLabel(updates.at(2)), u"Plasma Widget"_s);
    QCOMPARE(updates.at(2).icon, u"desktop"_s);
    QCOMPARE(updates.at(4).icon, u"package-x-generic"_s);
    QCOMPARE(updateSourceLabel(updates.at(5)), u"Nix"_s);
    QCOMPARE(updates.at(6).icon, u"package-x-generic"_s);
}

void UpdatesTest::parsesRestartState() {
    RestartState restart{.required = true, .stagedVersion = u"stale"_s};
    parseUpdateListing(uR"({"updates":[],"errors":[]})"_s, nullptr, nullptr, &restart);
    QVERIFY(!restart.required);
    QVERIFY(restart.stagedVersion.isEmpty());

    parseUpdateListing(uR"({"updates":[],"errors":[],"restart_required":true,"staged_version":"44.20261020.0"})"_s,
                       nullptr, nullptr, &restart);
    QVERIFY(restart.required);
    QCOMPARE(restart.stagedVersion, u"44.20261020.0"_s);

    const auto updates = parseUpdateListing(
        uR"({"updates":[{"id":"image:system","name":"Ultramarine Linux","backend":"bootc","kind":"package"}]})"_s);
    QCOMPARE(updateSourceLabel(updates.at(0)), u"System"_s);
}

void UpdatesTest::parsesOfflinePending() {
    RestartState restart;
    parseUpdateListing(uR"({"updates":[],"errors":[],"restart_required":true,"offline_pending":true})"_s, nullptr,
                       nullptr, &restart);
    QVERIFY(restart.required);
    QVERIFY(restart.offlinePending);
    QVERIFY(restart.stagedVersion.isEmpty());

    parseUpdateListing(uR"({"updates":[],"offline_pending":true})"_s, nullptr, nullptr, &restart);
    QVERIFY(restart.required);

    parseUpdateListing(uR"({"updates":[],"restart_required":true,"offline_pending":false,"staged_version":"45.1"})"_s,
                       nullptr, nullptr, &restart);
    QCOMPARE(restart, (RestartState{.required = true, .offlinePending = false, .stagedVersion = u"45.1"_s}));

    parseUpdateListing(uR"({"updates":[]})"_s, nullptr, nullptr, &restart);
    QCOMPARE(restart, RestartState{});
}

void UpdatesTest::refreshErrorMessages() {
    QCOMPARE(refreshErrorMessage(1, R"json({"updates":[],"errors":["RPM","Flatpak (system)"]})json", "warning\n"),
             u"Could not check for updates from: RPM, Flatpak (system)"_s);
    QCOMPARE(refreshErrorMessage(0, R"({"updates":[{"name":"foo"}],"errors":["Nix"]})", {}),
             u"Could not check for updates from: Nix"_s);
    QVERIFY(refreshErrorMessage(0, R"({"updates":[],"errors":[]})", {}).isEmpty());
    QCOMPARE(refreshErrorMessage(0, R"({"updates":[],"errors":[],"offline_error":"Transaction failed"})", {}),
             u"The updates could not be installed while restarting: Transaction failed"_s);
    QCOMPARE(refreshErrorMessage(0, R"({"updates":[],"errors":["Nix"],"offline_error":"Transaction failed"})", {}),
             u"The updates could not be installed while restarting: Transaction failed\n"
             u"Could not check for updates from: Nix"_s);

    QCOMPARE(refreshErrorMessage(2, "{\"updates\":", "Unknown option 'json'.\n"),
             u"Checking for updates failed (exit code 2). Unknown option 'json'."_s);
    QCOMPARE(refreshErrorMessage(1, {}, {}), u"Checking for updates failed (exit code 1)."_s);
    QVERIFY(refreshErrorMessage(-1, {}, {}).startsWith(u"Checking for updates failed:"_s));
    QVERIFY(refreshErrorMessage(0, "not json", {}).startsWith(u"Checking for updates failed:"_s));
}

void UpdatesTest::upgradeSettingsDefaults() {
    constexpr UpgradeSettings defaults;
    QVERIFY(defaults.automatic);
    QVERIFY(!defaults.offline);
    QCOMPARE(defaults.inactiveMinutes, 5);
    QVERIFY(!defaults.windowEnabled);
    QCOMPARE(defaults.windowStart, 60);
    QCOMPARE(defaults.windowEnd, 5 * 60);
    QVERIFY(defaults.isValid());

    UpgradeSettings changed = defaults;
    QVERIFY(changed == defaults);
    changed.windowEnd = changed.windowStart;
    QVERIFY(changed == defaults);
    QVERIFY(changed.isValid());
    changed.windowEnabled = true;
    QVERIFY(changed != defaults);
    QVERIFY(!changed.isValid());
    changed.windowEnd = 0;
    QVERIFY(changed.isValid());
}

void UpdatesTest::parsesUpgradeSettings() {
    UpgradeSettings settings;
    QVERIFY(parseUpgradeSettings(R"({"automatic":false,"offline":true})", &settings));
    QVERIFY(!settings.automatic);
    QVERIFY(settings.offline);

    QVERIFY(parseUpgradeSettings(
        R"({"automatic":true,"offline":false,"inactive_minutes":15,"window_start":"22:00","window_end":"06:30"})",
        &settings));
    QCOMPARE(settings.inactiveMinutes, 15);
    QVERIFY(settings.windowEnabled);
    QCOMPARE(settings.windowStart, 22 * 60);
    QCOMPARE(settings.windowEnd, 6 * 60 + 30);
    QCOMPARE(upgradeSettingsArguments(settings),
             QStringList({u"settings"_s, u"--automatic=true"_s, u"--offline=false"_s, u"--inactive-minutes=15"_s,
                          u"--window=22:00-06:30"_s}));

    QVERIFY(parseUpgradeSettings(R"({"window_start":"","window_end":""})", &settings));
    QVERIFY(!settings.windowEnabled);
    QCOMPARE(upgradeSettingsArguments(settings).constLast(), u"--window="_s);

    QVERIFY(parseUpgradeSettings("{}", &settings));
    QVERIFY(settings == UpgradeSettings{});
    QVERIFY(!parseUpgradeSettings("Automatic updates: true", &settings));
}

void UpdatesTest::helperAcceptsUpgradeSettingsArguments() {
    const auto check = [](const UpgradeSettings &settings) {
        const QStringList arguments = upgradeSettingsArguments(settings);
        QCommandLineParser parser;
        UltramarineUpdates::setUpParser(parser);
        QVERIFY2(parser.parse(QStringList{u"ultramarine-updates"_s} + arguments), qPrintable(parser.errorText()));
        QCOMPARE(parser.positionalArguments(), QStringList{arguments.constFirst()});
        const QString invalid = UltramarineUpdates::checkOptions(arguments.constFirst(), parser);
        QVERIFY2(invalid.isEmpty(), qPrintable(invalid));

        UltramarineUpdates::Settings parsed;
        QString error;
        QVERIFY2(
            UltramarineUpdates::parseWindow(parser.value(u"window"_s), &parsed.windowStart, &parsed.windowEnd, &error),
            qPrintable(error));
        parsed.inactiveMinutes = parser.value(u"inactive-minutes"_s).toInt();
        const QString validation = parsed.validate();
        QVERIFY2(validation.isEmpty(), qPrintable(validation));
        QCOMPARE(parsed.windowStart.isEmpty(), !settings.windowEnabled);
    };

    check(UpgradeSettings{});
    UpgradeSettings settings;
    settings.automatic = false;
    settings.offline = true;
    settings.inactiveMinutes = 0;
    check(settings);
    settings.inactiveMinutes = 24 * 60;
    settings.windowEnabled = true;
    settings.windowStart = 22 * 60;
    settings.windowEnd = 6 * 60 + 30;
    check(settings);
    settings.windowStart = 0;
    settings.windowEnd = 24 * 60 - 1;
    check(settings);
}

void UpdatesTest::parsesScheduleStatus() {
    bool scheduled = false;
    bool partial = true;
    QVERIFY(parseScheduleStatus(R"({"scheduled":true,"next":1792656000})", &scheduled, &partial));
    QVERIFY(scheduled);
    QVERIFY(!partial);
    QVERIFY(parseScheduleStatus(R"({"scheduled":false})", &scheduled));
    QVERIFY(!scheduled);
    QVERIFY(parseScheduleStatus(R"({"scheduled":false,"partial":true})", &scheduled, &partial));
    QVERIFY(!scheduled);
    QVERIFY(partial);
    QVERIFY(!parseScheduleStatus("nope", &scheduled, &partial));
}

void UpdatesTest::upgradeSettingsIgnoreUnusedWindow() {
    UpgradeSettings saved;
    UpgradeSettings edited = saved;
    edited.windowStart = 22 * 60;
    edited.windowEnd = 6 * 60;
    QVERIFY(edited == saved);

    edited.windowEnabled = true;
    saved.windowEnabled = true;
    QVERIFY(edited != saved);
    saved.windowStart = edited.windowStart;
    saved.windowEnd = edited.windowEnd;
    QVERIFY(edited == saved);

    for (const auto change:
         {+[](UpgradeSettings &s) { s.automatic = !s.automatic; }, +[](UpgradeSettings &s) { s.offline = !s.offline; },
          +[](UpgradeSettings &s) { s.inactiveMinutes += 1; },
          +[](UpgradeSettings &s) { s.windowEnabled = !s.windowEnabled; }}) {
        UpgradeSettings other = saved;
        change(other);
        QVERIFY(other != saved);
    }
}

void UpdatesTest::installErrorMessages() {
    QCOMPARE(installErrorMessage(0, QProcess::CrashExit, "partial log\n"),
             u"Updating failed: the update helper stopped unexpectedly."_s);
    QCOMPARE(installErrorMessage(1, QProcess::NormalExit, {}), u"Updating failed (exit code 1)."_s);
    QByteArray stdErr;
    for (int line = 1; line <= 15; ++line) {
        stdErr += "line " + QByteArray::number(line) + '\n';
    }
    const QString message = installErrorMessage(1, QProcess::NormalExit, stdErr);
    QVERIFY(message.startsWith(u"Updating failed (exit code 1). line 6\n"_s));
    QVERIFY(message.endsWith(u"\nline 15"_s));
    QVERIFY(!message.contains(u"line 5\n"_s));
    QCOMPARE(installErrorMessage(1, QProcess::NormalExit, "Error: no updates could be downloaded\n"),
             u"Updating failed (exit code 1). Error: no updates could be downloaded"_s);
    QCOMPARE(installErrorMessage(1, QProcess::NormalExit, "Fehler: ungültig"),
             u"Updating failed (exit code 1). Fehler: ungültig"_s);
    QVERIFY(installErrorMessage(126, QProcess::NormalExit, {}).startsWith(u"Authentication"_s));
}

void UpdatesTest::previewModelLimitsRows() {
    UpdatesModel model;
    const UpdatesPreviewModel preview(&model, 2);
    QCOMPARE(preview.rowCount(), 0);
    for (const QString &name: {u"a"_s, u"b"_s, u"c"_s}) {
        UpdateItem item;
        item.name = name;
        model.addUpdate(item);
    }
    QCOMPARE(preview.limit(), 2);
    QCOMPARE(preview.rowCount(), 2);
    QCOMPARE(preview.data(preview.index(1, 0), UpdatesModel::NameRole).toString(), u"b"_s);
    model.clear();
    QCOMPARE(preview.rowCount(), 0);
}

void UpdatesTest::composesErrorMessages() {
    QVERIFY(composeErrorMessage({}, {}).isEmpty());
    QCOMPARE(composeErrorMessage(u"install"_s, {}), u"install"_s);
    QCOMPARE(composeErrorMessage({}, u"check"_s), u"check"_s);
    QCOMPARE(composeErrorMessage(u"install"_s, u"check"_s), u"install\ncheck"_s);
}

void UpdatesTest::findsLastOutputLine() {
    QVERIFY(lastOutputLine({}).isEmpty());
    QVERIFY(lastOutputLine(" \n\t\r\n").isEmpty());
    QCOMPARE(lastOutputLine("only"), u"only"_s);
    QCOMPARE(lastOutputLine("first\nsecond\n"), u"second"_s);
    QCOMPARE(lastOutputLine("first\n  Downloading foo  \r\n\n \n"), u"Downloading foo"_s);
    QCOMPARE(lastOutputLine("\nlast"), u"last"_s);
    QCOMPARE(lastOutputLine("a\nWird ausgeführt\n"), u"Wird ausgeführt"_s);
}

QTEST_GUILESS_MAIN(UpdatesTest)

#include "test_updates.moc"
