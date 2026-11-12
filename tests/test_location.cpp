// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include "location.h"

using namespace Qt::StringLiterals;

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
    QCOMPARE(parseUnitFileState(u"static"_s), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(u"enabled"_s), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(u"disabled"_s), LocationState::Enabled);
    QCOMPARE(parseUnitFileState(u"masked"_s), LocationState::Disabled);
    QCOMPARE(parseUnitFileState(u"masked-runtime"_s), LocationState::DisabledRuntime);
    QCOMPARE(parseUnitFileState(u"not-found"_s), LocationState::Unavailable);
    QCOMPARE(parseUnitFileState(u"bad"_s), LocationState::ReadFailed);
    QCOMPARE(parseUnitFileState(QString()), LocationState::Unavailable);
}

void LocationTest::parsesUnitFileStateErrors() {
    // systemd 259 answers GetUnitFileState for a missing unit with FileNotFound
    QCOMPARE(parseUnitFileStateError(u"org.freedesktop.DBus.Error.FileNotFound"_s), LocationState::Unavailable);
    QCOMPARE(parseUnitFileStateError(u"org.freedesktop.systemd1.NoSuchUnit"_s), LocationState::Unavailable);
    QCOMPARE(parseUnitFileStateError(u"org.freedesktop.DBus.Error.NoReply"_s), LocationState::ReadFailed);
    QCOMPARE(parseUnitFileStateError(u"org.freedesktop.DBus.Error.AccessDenied"_s), LocationState::ReadFailed);
    QCOMPARE(parseUnitFileStateError(QString()), LocationState::ReadFailed);
}

void LocationTest::reportsHelperResults() {
    QVERIFY(helperErrorMessage(0, QProcess::NormalExit, u"Created symlink"_s).isEmpty());

    constexpr int PkexecDismissed = 126;
    for (constexpr int PkexecNotAuthorized = 127; const int code: {PkexecDismissed, PkexecNotAuthorized}) {
        const QString message = helperErrorMessage(code, QProcess::NormalExit, {});
        QVERIFY(message.contains("No changes were made"_L1));
    }

    const QString failedAfterAuthentication = helperErrorMessage(1, QProcess::NormalExit, u"  Failed to stop\n"_s);
    QVERIFY(!failedAfterAuthentication.contains("No changes were made"_L1));
    QVERIFY(failedAfterAuthentication.contains("partly applied"_L1));
    QVERIFY(failedAfterAuthentication.endsWith("Failed to stop"_L1));

    QVERIFY(!helperErrorMessage(PkexecDismissed, QProcess::CrashExit, {}).contains("No changes were made"_L1));
    QVERIFY(helperErrorMessage(1, QProcess::NormalExit, {}).contains("partly applied"_L1));
}

QTEST_GUILESS_MAIN(LocationTest)

#include "test_location.moc"
