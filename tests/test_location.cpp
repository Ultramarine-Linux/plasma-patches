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
    QCOMPARE(parseUnitFileState(QStringLiteral("static")), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("enabled")), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("disabled")), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("masked")), LocationState::Disabled);
    QCOMPARE(parseUnitFileState(QStringLiteral("masked-runtime")), LocationState::DisabledRuntime);
    QCOMPARE(parseUnitFileState(QStringLiteral("not-found")), LocationState::Unavailable);
    QCOMPARE(parseUnitFileState(QStringLiteral("bad")), LocationState::ReadFailed);
    QCOMPARE(parseUnitFileState(QString()), LocationState::Unavailable);
}

void LocationTest::parsesUnitFileStateErrors() {
    // systemd 259 answers GetUnitFileState for a missing unit with FileNotFound
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.DBus.Error.FileNotFound")),
             LocationState::Unavailable);
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.systemd1.NoSuchUnit")),
             LocationState::Unavailable);
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.DBus.Error.NoReply")), LocationState::ReadFailed);
    QCOMPARE(parseUnitFileStateError(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied")),
             LocationState::ReadFailed);
    QCOMPARE(parseUnitFileStateError(QString()), LocationState::ReadFailed);
}

void LocationTest::reportsHelperResults() {
    QVERIFY(helperErrorMessage(0, QProcess::NormalExit, QStringLiteral("Created symlink")).isEmpty());

    constexpr int PkexecDismissed = 126;
    constexpr int PkexecNotAuthorized = 127;
    for (const int code: {PkexecDismissed, PkexecNotAuthorized}) {
        const QString message = helperErrorMessage(code, QProcess::NormalExit, {});
        QVERIFY(message.contains(QLatin1String("No changes were made")));
    }

    const QString failedAfterAuthentication =
        helperErrorMessage(1, QProcess::NormalExit, QStringLiteral("  Failed to stop\n"));
    QVERIFY(!failedAfterAuthentication.contains(QLatin1String("No changes were made")));
    QVERIFY(failedAfterAuthentication.contains(QLatin1String("partly applied")));
    QVERIFY(failedAfterAuthentication.endsWith(QLatin1String("Failed to stop")));

    QVERIFY(
        !helperErrorMessage(PkexecDismissed, QProcess::CrashExit, {}).contains(QLatin1String("No changes were made")));
    QVERIFY(helperErrorMessage(1, QProcess::NormalExit, {}).contains(QLatin1String("partly applied")));
}

QTEST_GUILESS_MAIN(LocationTest)

#include "test_location.moc"
