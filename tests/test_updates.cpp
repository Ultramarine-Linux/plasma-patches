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
        static void parsesUmOutput();
        static void rejectsInvalidUmOutput();
        static void parsesUmErrors();
        static void listsAppsFirst();
        static void parsesUpgradeSettings();
        static void parsesScheduleStatus();
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

void UpdatesTest::parsesUmOutput() {
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

void UpdatesTest::rejectsInvalidUmOutput() {
    auto technical = QStringLiteral("unchanged");
    QVERIFY(parseUpdateListing(QStringLiteral("not json"), &technical).isEmpty());
    QCOMPARE(technical, QStringLiteral("unchanged"));
}

void UpdatesTest::parsesUmErrors() {
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
        {"id":"nix:flox","name":"flox","source":"Nix (github:flox/flox/latest)","backend":"nix","kind":"package","icon":"package-x-generic"}
    ]})json"));

    QStringList names;
    for (const UpdateItem &item: updates) {
        names << item.name;
    }
    QCOMPARE(names,
             QStringList({QStringLiteral("Firefox"), QStringLiteral("Haruna"), QStringLiteral("glibc"),
                          QStringLiteral("bash"), QStringLiteral("flox"), QStringLiteral("GNOME Platform")}));
    QCOMPARE(updateSourceLabel(updates.at(4)), QStringLiteral("Nix"));
    QCOMPARE(updates.at(0).appId, QStringLiteral("org.mozilla.firefox"));
    QCOMPARE(updateSourceLabel(updates.at(1)), QStringLiteral("Flatpak"));
    // Runtimes never get their own icon, even if the helper reports one.
    QCOMPARE(updates.at(5).icon, QStringLiteral("package-x-generic"));
    // No icon reported: fall back to the generic one.
    QCOMPARE(updates.at(3).icon, QStringLiteral("package-x-generic"));
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

    // An empty window means any time.
    QVERIFY(parseUpgradeSettings(R"({"window_start":"","window_end":""})", &settings));
    QVERIFY(!settings.windowEnabled);
    QCOMPARE(upgradeSettingsArguments(settings).constLast(), QStringLiteral("--window="));

    // Missing keys keep the defaults.
    QVERIFY(parseUpgradeSettings("{}", &settings));
    QVERIFY(settings == UpgradeSettings{});
    QVERIFY(!parseUpgradeSettings("Automatic updates: true", &settings));
}

void UpdatesTest::parsesScheduleStatus() {
    bool scheduled = false;
    qint64 next = 0;
    QVERIFY(parseScheduleStatus(R"({"scheduled":true,"next":1792656000})", &scheduled, &next));
    QVERIFY(scheduled);
    QCOMPARE(next, 1792656000);
    QVERIFY(parseScheduleStatus(R"({"scheduled":false})", &scheduled, &next));
    QVERIFY(!scheduled);
    QCOMPARE(next, 0);
    QVERIFY(!parseScheduleStatus("nope", &scheduled, &next));
}

QTEST_GUILESS_MAIN(UpdatesTest)

#include "test_updates.moc"
