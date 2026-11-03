// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTest>

#include "updates.h"

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
        static void installErrorMessages();
        static void previewModelLimitsRows();
        static void composesErrorMessages();
    };
} // namespace

void UpdatesTest::modelExposesUpdateRoles() {
    UpdatesModel model;
    const QSignalSpy countChanged(&model, &UpdatesModel::countChanged);

    UpdateItem item;
    item.name = QStringLiteral("firefox");
    item.source = QStringLiteral("fedora");
    item.oldVersion = QStringLiteral("1");
    item.newVersion = QStringLiteral("2");
    item.icon = QStringLiteral("firefox");
    item.kind = QStringLiteral("app");
    item.backend = QStringLiteral("rpm");
    item.appId = QStringLiteral("org.mozilla.firefox");
    model.addUpdate(item);

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.rowCount({}), 1);
    QCOMPARE(countChanged.count(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, UpdatesModel::NameRole).toString(), QStringLiteral("firefox"));
    QCOMPARE(model.data(index, UpdatesModel::SourceRole).toString(), QStringLiteral("fedora"));
    QCOMPARE(model.data(index, UpdatesModel::OldVersionRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(index, UpdatesModel::NewVersionRole).toString(), QStringLiteral("2"));
    QCOMPARE(model.data(index, UpdatesModel::IconRole).toString(), QStringLiteral("firefox"));
    QCOMPARE(model.data(index, UpdatesModel::KindRole).toString(), QStringLiteral("app"));
    QCOMPARE(model.data(index, UpdatesModel::AppIdRole).toString(), QStringLiteral("org.mozilla.firefox"));
    QCOMPARE(model.data(index, UpdatesModel::SourceLabelRole).toString(), QStringLiteral("System"));
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
    QCOMPARE(model.countExcluding(QStringLiteral("rpm")), 0);

    for (const QString &backend: {QStringLiteral("rpm"), QStringLiteral("flatpak"), QStringLiteral("plasmoid")}) {
        UpdateItem item;
        item.backend = backend;
        model.addUpdate(item);
    }
    QCOMPARE(model.countExcluding(QStringLiteral("rpm")), 2);
    QCOMPARE(model.countExcluding(QStringLiteral("nix")), 3);
}

void UpdatesTest::parsesUpdateListing() {
    QString technical;
    const auto updates = parseUpdateListing(
        QStringLiteral(
            R"({"updates":[{"name":"foo","source":"fedora","old_version":"1.0","new_version":"2.0"},{"name":"bar","source":"flathub","old_version":"3","new_version":"4","icon":"bar-icon"}]})"),
        &technical);

    QCOMPARE(updates.size(), 2);
    QCOMPARE(updates.at(0).icon, QStringLiteral("package-x-generic"));
    QCOMPARE(updates.at(1).icon, QStringLiteral("bar-icon"));
    QCOMPARE(technical, QStringLiteral("foo: 1.0 → 2.0 (fedora)\nbar: 3 → 4 (flathub)\n"));
}

void UpdatesTest::rejectsInvalidUpdateListing() {
    auto technical = QStringLiteral("unchanged");
    QVERIFY(parseUpdateListing(QStringLiteral("not json"), &technical).isEmpty());
    QCOMPARE(technical, QStringLiteral("unchanged"));
}

void UpdatesTest::parsesListingErrors() {
    QStringList errors{QStringLiteral("stale")};
    const auto updates = parseUpdateListing(
        QStringLiteral(
            R"json({"updates":[{"id":"foo.x86_64","name":"foo","source":"RPM (fedora)","old_version":"1","new_version":"2"}],"errors":["flatpak (user): failed"]})json"),
        nullptr, &errors);
    QCOMPARE(updates.size(), 1);
    QCOMPARE(errors, QStringList{QStringLiteral("flatpak (user): failed")});

    parseUpdateListing(QStringLiteral(R"({"updates":[],"errors":[]})"), nullptr, &errors);
    QVERIFY(errors.isEmpty());
}

void UpdatesTest::listsAppsFirst() {
    const auto updates = parseUpdateListing(QStringLiteral(R"json({"updates":[
        {"id":"glibc.x86_64","name":"glibc","source":"RPM (updates)","backend":"rpm","kind":"package","icon":"package-x-generic"},
        {"id":"org.gnome.Platform//51","name":"GNOME Platform","source":"Flatpak (flathub)","backend":"flatpak","kind":"runtime","icon":"org.gnome.Platform"},
        {"id":"firefox.x86_64","name":"Firefox","source":"RPM (updates)","backend":"rpm","kind":"app","icon":"firefox","app_id":"org.mozilla.firefox"},
        {"id":"bash.x86_64","name":"bash","source":"RPM (updates)","backend":"rpm","kind":"package"},
        {"id":"org.kde.haruna//stable","name":"Haruna","source":"Flatpak (flathub)","backend":"flatpak","kind":"app","icon":"org.kde.haruna","app_id":"org.kde.haruna"},
        {"id":"nix:flox","name":"flox","source":"Nix (github:flox/flox/latest)","backend":"nix","kind":"package","icon":"package-x-generic"},
        {"id":"plasmoid:2130967","name":"Panel Colorizer","source":"KDE Store","backend":"plasmoid","kind":"addon","icon":"desktop"}
    ]})json"));

    QStringList names;
    for (const UpdateItem &item: updates) {
        names << item.name;
    }
    QCOMPARE(names,
             QStringList({QStringLiteral("Firefox"), QStringLiteral("Haruna"), QStringLiteral("Panel Colorizer"),
                          QStringLiteral("glibc"), QStringLiteral("bash"), QStringLiteral("flox"),
                          QStringLiteral("GNOME Platform")}));
    QCOMPARE(updateSourceLabel(updates.at(5)), QStringLiteral("Nix"));
    QCOMPARE(updateSourceLabel(updates.at(2)), QStringLiteral("Plasma Widget"));
    QCOMPARE(updates.at(2).icon, QStringLiteral("desktop"));
    QCOMPARE(updates.at(0).appId, QStringLiteral("org.mozilla.firefox"));
    QCOMPARE(updateSourceLabel(updates.at(1)), QStringLiteral("Flatpak"));
    // runtimes never get their own icon, even if the helper reports one
    QCOMPARE(updates.at(6).icon, QStringLiteral("package-x-generic"));
    // no icon reported: fall back to the generic one
    QCOMPARE(updates.at(4).icon, QStringLiteral("package-x-generic"));
}

void UpdatesTest::parsesRestartState() {
    RestartState restart{.required = true, .stagedVersion = QStringLiteral("stale")};
    parseUpdateListing(QStringLiteral(R"({"updates":[],"errors":[]})"), nullptr, nullptr, &restart);
    QVERIFY(!restart.required);
    QVERIFY(restart.stagedVersion.isEmpty());

    parseUpdateListing(
        QStringLiteral(R"({"updates":[],"errors":[],"restart_required":true,"staged_version":"44.20261020.0"})"),
        nullptr, nullptr, &restart);
    QVERIFY(restart.required);
    QCOMPARE(restart.stagedVersion, QStringLiteral("44.20261020.0"));

    // image updates are labeled as system updates
    const auto updates = parseUpdateListing(QStringLiteral(
        R"({"updates":[{"id":"image:system","name":"Ultramarine Linux","backend":"bootc","kind":"package"}]})"));
    QCOMPARE(updateSourceLabel(updates.at(0)), QStringLiteral("System"));
}

void UpdatesTest::parsesOfflinePending() {
    RestartState restart;
    parseUpdateListing(QStringLiteral(R"({"updates":[],"errors":[],"restart_required":true,"offline_pending":true})"),
                       nullptr, nullptr, &restart);
    QVERIFY(restart.required);
    QVERIFY(restart.offlinePending);
    QVERIFY(restart.stagedVersion.isEmpty());

    // pending offline updates always need a restart
    parseUpdateListing(QStringLiteral(R"({"updates":[],"offline_pending":true})"), nullptr, nullptr, &restart);
    QVERIFY(restart.required);

    // image staging keeps its version and isn't an offline update
    parseUpdateListing(
        QStringLiteral(R"({"updates":[],"restart_required":true,"offline_pending":false,"staged_version":"45.1"})"),
        nullptr, nullptr, &restart);
    QCOMPARE(restart,
             (RestartState{.required = true, .offlinePending = false, .stagedVersion = QStringLiteral("45.1")}));

    parseUpdateListing(QStringLiteral(R"({"updates":[]})"), nullptr, nullptr, &restart);
    QCOMPARE(restart, RestartState{});
}

void UpdatesTest::refreshErrorMessages() {
    // every source failed: `list` exits 1 but still prints a listing
    QCOMPARE(refreshErrorMessage(1, R"json({"updates":[],"errors":["RPM","Flatpak (system)"]})json", "warning\n"),
             QStringLiteral("Could not check for updates from: RPM, Flatpak (system)"));
    // some sources failed
    QCOMPARE(refreshErrorMessage(0, R"({"updates":[{"name":"foo"}],"errors":["Nix"]})", {}),
             QStringLiteral("Could not check for updates from: Nix"));
    QVERIFY(refreshErrorMessage(0, R"({"updates":[],"errors":[]})", {}).isEmpty());
    // a failed offline update isn't a source that couldn't be checked
    QCOMPARE(refreshErrorMessage(0, R"({"updates":[],"errors":[],"offline_error":"Transaction failed"})", {}),
             QStringLiteral("The updates could not be installed while restarting: Transaction failed"));
    QCOMPARE(refreshErrorMessage(0, R"({"updates":[],"errors":["Nix"],"offline_error":"Transaction failed"})", {}),
             QStringLiteral("The updates could not be installed while restarting: Transaction failed\n"
                            "Could not check for updates from: Nix"));

    // no listing: stdout is never shown
    QCOMPARE(refreshErrorMessage(2, "{\"updates\":", "Unknown option 'json'.\n"),
             QStringLiteral("Checking for updates failed (exit code 2). Unknown option 'json'."));
    QCOMPARE(refreshErrorMessage(1, {}, {}), QStringLiteral("Checking for updates failed (exit code 1)."));
    QVERIFY(refreshErrorMessage(-1, {}, {}).startsWith(QStringLiteral("Checking for updates failed:")));
    QVERIFY(refreshErrorMessage(0, "not json", {}).startsWith(QStringLiteral("Checking for updates failed:")));
}

void UpdatesTest::upgradeSettingsDefaults() {
    const UpgradeSettings defaults;
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
    // the times aren't stored while the window is off, so they don't make it a change
    QVERIFY(changed == defaults);
    // an equal start and end only matters once the window is used, since the helper rejects it then
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
    QCOMPARE(
        upgradeSettingsArguments(settings),
        QStringList({QStringLiteral("settings"), QStringLiteral("--automatic=true"), QStringLiteral("--offline=false"),
                     QStringLiteral("--inactive-minutes=15"), QStringLiteral("--window=22:00-06:30")}));

    // an empty window means any time
    QVERIFY(parseUpgradeSettings(R"({"window_start":"","window_end":""})", &settings));
    QVERIFY(!settings.windowEnabled);
    QCOMPARE(upgradeSettingsArguments(settings).constLast(), QStringLiteral("--window="));

    // missing keys keep the defaults
    QVERIFY(parseUpgradeSettings("{}", &settings));
    QVERIFY(settings == UpgradeSettings{});
    QVERIFY(!parseUpgradeSettings("Automatic updates: true", &settings));
}

void UpdatesTest::parsesScheduleStatus() {
    bool scheduled = false;
    bool partial = true;
    QVERIFY(parseScheduleStatus(R"({"scheduled":true,"next":1792656000})", &scheduled, &partial));
    QVERIFY(scheduled);
    QVERIFY(!partial);
    QVERIFY(parseScheduleStatus(R"({"scheduled":false})", &scheduled));
    QVERIFY(!scheduled);
    // only the system or the user part is still scheduled
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

    // once the window is used, its times matter
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
             QStringLiteral("Updating failed: the update helper stopped unexpectedly."));
    QCOMPARE(installErrorMessage(1, QProcess::NormalExit, {}), QStringLiteral("Updating failed (exit code 1)."));
    // stdout (the package manager's log) is never passed, and only stderr's last lines are shown
    QByteArray stdErr;
    for (int line = 1; line <= 15; ++line) {
        stdErr += "line " + QByteArray::number(line) + '\n';
    }
    const QString message = installErrorMessage(1, QProcess::NormalExit, stdErr);
    QVERIFY(message.startsWith(QStringLiteral("Updating failed (exit code 1). line 6\n")));
    QVERIFY(message.endsWith(QStringLiteral("\nline 15")));
    QVERIFY(!message.contains(QStringLiteral("line 5\n")));
    // no special case for messages that mention updates
    QCOMPARE(installErrorMessage(1, QProcess::NormalExit, "Error: no updates could be downloaded\n"),
             QStringLiteral("Updating failed (exit code 1). Error: no updates could be downloaded"));
    // helper output is UTF-8 whatever the locale
    QCOMPARE(installErrorMessage(1, QProcess::NormalExit, "Fehler: ungültig"),
             QStringLiteral("Updating failed (exit code 1). Fehler: ungültig"));
    QVERIFY(installErrorMessage(126, QProcess::NormalExit, {}).startsWith(QStringLiteral("Authentication")));
}

void UpdatesTest::previewModelLimitsRows() {
    UpdatesModel model;
    const UpdatesPreviewModel preview(&model, 2);
    QCOMPARE(preview.rowCount(), 0);
    for (const QString &name: {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
        UpdateItem item;
        item.name = name;
        model.addUpdate(item);
    }
    QCOMPARE(preview.limit(), 2);
    QCOMPARE(preview.rowCount(), 2);
    QCOMPARE(preview.data(preview.index(1, 0), UpdatesModel::NameRole).toString(), QStringLiteral("b"));
    model.clear();
    QCOMPARE(preview.rowCount(), 0);
}

void UpdatesTest::composesErrorMessages() {
    QVERIFY(composeErrorMessage({}, {}).isEmpty());
    QCOMPARE(composeErrorMessage(QStringLiteral("install"), {}), QStringLiteral("install"));
    QCOMPARE(composeErrorMessage({}, QStringLiteral("check")), QStringLiteral("check"));
    // the install error comes first and the check's error below it, each shown once
    QCOMPARE(composeErrorMessage(QStringLiteral("install"), QStringLiteral("check")), QStringLiteral("install\ncheck"));
}

QTEST_GUILESS_MAIN(UpdatesTest)

#include "test_updates.moc"
