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
        static void parsesUnitFileStateErrors();
        static void reportsHelperResults();
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

void LocationTest::parsesUnitFileStateErrors() {
    // systemd 259 answers GetUnitFileState for a missing unit with FileNotFound
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.DBus.Error.FileNotFound")),
             LocationState::Unavailable);
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.systemd1.NoSuchUnit")),
             LocationState::Unavailable);
    // anything else doesn't say whether GeoClue is installed
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.DBus.Error.NoReply")), LocationState::ReadFailed);
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied")),
             LocationState::ReadFailed);
    QCOMPARE(parseUnitFileStateError(QString()), LocationState::ReadFailed);
}

void LocationTest::reportsHelperResults() {
    QVERIFY(helperErrorMessage(0, QProcess::NormalExit, QStringLiteral("Created symlink")).isEmpty());

    // pkexec's dismissed and not authorized codes
    for (const int code: {126, 127}) {
        const QString message = helperErrorMessage(code, QProcess::NormalExit, {});
        QVERIFY(message.contains(QLatin1String("No changes were made")));
    }

    // a failure after authentication may have changed something
    const QString failed = helperErrorMessage(1, QProcess::NormalExit, QStringLiteral("  Failed to stop\n"));
    QVERIFY(!failed.contains(QLatin1String("No changes were made")));
    QVERIFY(failed.contains(QLatin1String("partly applied")));
    QVERIFY(failed.endsWith(QLatin1String("Failed to stop")));

    // a crash is never an authentication failure, whatever the exit code
    QVERIFY(!helperErrorMessage(126, QProcess::CrashExit, {}).contains(QLatin1String("No changes were made")));
    QVERIFY(helperErrorMessage(1, QProcess::NormalExit, {}).contains(QLatin1String("partly applied")));
}

QTEST_GUILESS_MAIN(LocationTest)

#include "test_location.moc"
