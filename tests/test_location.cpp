// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "location.h"

namespace {
    class LocationTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void parsesUnitFileStates();
    };
} // namespace

void LocationTest::parsesUnitFileStates() {
    // GeoClue is D-Bus activated, so it's normally static
    QCOMPARE(parseUnitFileState(QStringLiteral("static")), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("enabled")), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("disabled")), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("masked")), LocationState::Disabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("masked-runtime")), LocationState::DisabledRuntime);
    QCOMPARE(parseUnitFileState(QStringLiteral("not-found")), LocationState::Unavailable);
    QCOMPARE(parseUnitFileState(QStringLiteral("bad")), LocationState::Unavailable);
    QCOMPARE(parseUnitFileState(QString()), LocationState::Unavailable);
}

QTEST_GUILESS_MAIN(LocationTest)

#include "test_location.moc"
