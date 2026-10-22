// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QSignalSpy>
#include <QTest>

#include "updates.h"

class UpdatesTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void modelExposesUpdateRoles();
    void modelClearOnlyNotifiesWhenNeeded();
    void parsesUmOutput();
    void rejectsInvalidUmOutput();
};

void UpdatesTest::modelExposesUpdateRoles() {
    UpdatesModel model;
    QSignalSpy countChanged(&model, &UpdatesModel::countChanged);

    model.addUpdate({QStringLiteral("firefox"), QStringLiteral("fedora"), QStringLiteral("1"), QStringLiteral("2"),
                     QStringLiteral("firefox")});

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.rowCount({}), 1);
    QCOMPARE(countChanged.count(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, UpdatesModel::NameRole).toString(), QStringLiteral("firefox"));
    QCOMPARE(model.data(index, UpdatesModel::SourceRole).toString(), QStringLiteral("fedora"));
    QCOMPARE(model.data(index, UpdatesModel::OldVersionRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(index, UpdatesModel::NewVersionRole).toString(), QStringLiteral("2"));
    QCOMPARE(model.data(index, UpdatesModel::IconRole).toString(), QStringLiteral("firefox"));
    QVERIFY(!model.data({}, UpdatesModel::NameRole).isValid());
}

void UpdatesTest::modelClearOnlyNotifiesWhenNeeded() {
    UpdatesModel model;
    QSignalSpy countChanged(&model, &UpdatesModel::countChanged);

    model.clear();
    QCOMPARE(countChanged.count(), 0);
    model.addUpdate({});
    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(countChanged.count(), 2);
}

void UpdatesTest::parsesUmOutput() {
    QString technical;
    const auto updates = parseUmOutput(
        QStringLiteral(R"({"updates":[{"name":"foo","source":"fedora","old_version":"1.0","new_version":"2.0"},{"name":"bar","source":"flathub","old_version":"3","new_version":"4","icon":"bar-icon"}]})"),
        &technical);

    QCOMPARE(updates.size(), 2);
    QCOMPARE(updates.at(0).icon, QStringLiteral("package-x-generic"));
    QCOMPARE(updates.at(1).icon, QStringLiteral("bar-icon"));
    QCOMPARE(technical, QStringLiteral("foo: 1.0 → 2.0 (fedora)\nbar: 3 → 4 (flathub)\n"));
}

void UpdatesTest::rejectsInvalidUmOutput() {
    QString technical = QStringLiteral("unchanged");
    QVERIFY(parseUmOutput(QStringLiteral("not json"), &technical).isEmpty());
    QCOMPARE(technical, QStringLiteral("unchanged"));
}

QTEST_MAIN(UpdatesTest)

#include "test_updates.moc"
