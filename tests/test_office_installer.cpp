// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QTest>

#include <algorithm>
#include <cstdint>

#include "packagekit.h"
#include "suites.h"

using namespace Qt::StringLiterals;
using namespace OfficeInstaller;

namespace {
    constexpr std::uint32_t PkInfoInstalled = 1;
    constexpr std::uint32_t PkInfoAvailable = 2;
    constexpr uint PkExitFailed = 2;
    constexpr uint PkExitCancelled = 3;
    constexpr uint PkExitKeyRequired = 4;
    constexpr uint PkExitNeedUntrusted = 8;
    constexpr uint PkExitCancelledPriority = 9;
    constexpr uint PkErrorTransactionCancelled = 17;
    constexpr uint PkErrorCannotCancel = 30;
    constexpr uint UnrelatedPkError = 13;

    Suite suiteWithFlatpak(const QString &flatpakId) {
        const QList<Suite> all = offeredSuites();
        const auto suite = std::ranges::find(all, flatpakId, &Suite::flatpakId);
        return suite == all.end() ? Suite{} : *suite;
    }

    class OfficeInstallerTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void recommendsCollaboraFirst();
        static void defaultsToFlatpak();
        static void flatpakRefs();
        static void openFilesCommands();
        static void recognizesInstalledSuites();
        static void picksPackagesToInstall();
        static void explainsFailures();
        static void reportsCancelOnlyWhenCanceled();
    };
} // namespace

void OfficeInstallerTest::recommendsCollaboraFirst() {
    const QList<Suite> all = offeredSuites();
    QVERIFY(!all.isEmpty());
    QCOMPARE(all.constFirst().flatpakId, u"com.collaboraoffice.Office"_s);
    QVERIFY(all.constFirst().recommended);
    QCOMPARE(std::ranges::count_if(all, &Suite::recommended), 1);
}

void OfficeInstallerTest::defaultsToFlatpak() {
    for (const Suite &suite: offeredSuites()) {
        QCOMPARE(installSources(suite, false).value(0), Source::Flatpak);
        QCOMPARE(installSources(suite, true), QList{Source::Flatpak});
    }
    QCOMPARE(installSources(suiteWithFlatpak(u"com.collaboraoffice.Office"_s), false),
             (QList{Source::Flatpak, Source::Rpm}));
    QCOMPARE(installSources(suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s), false),
             (QList{Source::Flatpak, Source::Rpm}));
}

void OfficeInstallerTest::flatpakRefs() {
    QCOMPARE(flatpakRef(suiteWithFlatpak(u"com.collaboraoffice.Office"_s), u"x86_64"_s),
             u"app/com.collaboraoffice.Office/x86_64/stable"_s);
    QCOMPARE(flatpakRef(suiteWithFlatpak(u"org.onlyoffice.desktopeditors"_s), u"aarch64"_s),
             u"app/org.onlyoffice.desktopeditors/aarch64/stable"_s);
}

void OfficeInstallerTest::openFilesCommands() {
    const Suite collabora = suiteWithFlatpak(u"com.collaboraoffice.Office"_s);
    QCOMPARE(openFilesCommand(collabora, Source::Flatpak, {u"/home/a/b c.docx"_s}),
             (Command{.program = u"flatpak"_s,
                      .arguments = {u"run"_s, u"--file-forwarding"_s, u"com.collaboraoffice.Office"_s, u"@@"_s,
                                    u"/home/a/b c.docx"_s, u"@@"_s}}));
    QCOMPARE(openFilesCommand(collabora, Source::Flatpak, {}),
             (Command{.program = u"flatpak"_s, .arguments = {u"run"_s, u"com.collaboraoffice.Office"_s}}));
    QCOMPARE(openFilesCommand(suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s), Source::Rpm, {u"/tmp/x.odt"_s}),
             (Command{.program = u"libreoffice"_s, .arguments = {u"/tmp/x.odt"_s}}));
    QCOMPARE(openFilesCommand(collabora, Source::Rpm, {u"/tmp/x.odt"_s}),
             (Command{.program = u"coda-qt"_s, .arguments = {u"/tmp/x.odt"_s}}));
}

void OfficeInstallerTest::recognizesInstalledSuites() {
    const Suite libreoffice = suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s);
    const Suite collabora = suiteWithFlatpak(u"com.collaboraoffice.Office"_s);
    const Suite onlyoffice = suiteWithFlatpak(u"org.onlyoffice.desktopeditors"_s);
    QVERIFY(isSuite(libreoffice, u"org.libreoffice.LibreOffice.writer"_s, u"flatpak"_s));
    QVERIFY(isSuite(onlyoffice, u"org.onlyoffice.desktopeditors"_s, u"flatpak"_s));
    QVERIFY(!isSuite(libreoffice, u"org.libreoffice.LibreOfficeX"_s, u"flatpak"_s));
    QVERIFY(!isSuite(onlyoffice, u"com.collaboraoffice.Office"_s, u"flatpak"_s));
    QVERIFY(isSuite(libreoffice, u"libreoffice-writer"_s, u"libreoffice"_s));
    QVERIFY(isSuite(collabora, u"coda"_s, u"coda-qt"_s));
    QVERIFY(!isSuite(collabora, u"libreoffice-writer"_s, u"libreoffice"_s));
    QVERIFY(!isSuite(onlyoffice, u"something"_s, QString()));
}

void OfficeInstallerTest::picksPackagesToInstall() {
    const QList<ResolvedPackage> resolved{
        {.pkInfo = PkInfoAvailable, .packageId = u"libreoffice-writer;25.8.1-1.fc44;x86_64;updates"_s},
        {.pkInfo = PkInfoInstalled, .packageId = u"libreoffice-calc;25.8.1-1.fc44;x86_64;installed"_s},
        {.pkInfo = PkInfoAvailable, .packageId = u"libreoffice-calc;25.8.2-1.fc44;x86_64;updates"_s},
        {.pkInfo = PkInfoInstalled, .packageId = u"libreoffice-impress;25.8.1-1.fc44;x86_64;installed"_s},
        {.pkInfo = PkInfoAvailable, .packageId = u"libreoffice-writer-extras;1-1;x86_64;fedora"_s},
    };
    QStringList missing;
    QCOMPARE(packageIdsToInstall(resolved, {u"libreoffice-writer"_s, u"libreoffice-calc"_s, u"libreoffice-impress"_s},
                                 &missing),
             (QStringList{u"libreoffice-writer;25.8.1-1.fc44;x86_64;updates"_s,
                          u"libreoffice-calc;25.8.2-1.fc44;x86_64;updates"_s}));
    QVERIFY(missing.isEmpty());

    QCOMPARE(packageIdsToInstall(resolved, {u"collabora-office"_s, u"libreoffice-impress"_s}, &missing), QStringList());
    QCOMPARE(missing, QStringList{u"collabora-office"_s});
}

void OfficeInstallerTest::explainsFailures() {
    QCOMPARE(failureReason(PkExitFailed, u"Disk full"_s, {}), u"Disk full"_s);
    QVERIFY(failureReason(PkExitFailed, {}, {}).isEmpty());
    QVERIFY(failureReason(PkExitKeyRequired, {}, u"terra"_s).contains(u"terra"_s));
    QVERIFY(!failureReason(PkExitKeyRequired, {}, {}).isEmpty());
    QVERIFY(!failureReason(PkExitNeedUntrusted, {}, {}).isEmpty());
    QVERIFY(!failureReason(PkExitCancelledPriority, {}, {}).isEmpty());
}

void OfficeInstallerTest::reportsCancelOnlyWhenCanceled() {
    TransactionFailure failure = transactionFailure(PkExitCancelled, 0, {}, {});
    QVERIFY(failure.canceled);
    QVERIFY(failure.error.isEmpty());

    failure = transactionFailure(PkExitFailed, PkErrorTransactionCancelled, u"Cancelled"_s, {});
    QVERIFY(failure.canceled);
    QVERIFY(failure.error.isEmpty());

    failure = transactionFailure(PkExitFailed, UnrelatedPkError, u"Nothing provides libfoo"_s, {});
    QVERIFY(!failure.canceled);
    QCOMPARE(failure.error, u"Nothing provides libfoo"_s);

    failure = transactionFailure(PkExitFailed, PkErrorCannotCancel, u"Cannot cancel"_s, {});
    QVERIFY(!failure.canceled);
    QCOMPARE(failure.error, u"Cannot cancel"_s);

    failure = transactionFailure(PkExitKeyRequired, 0, {}, u"terra"_s);
    QVERIFY(!failure.canceled);
    QVERIFY(failure.error.contains(u"terra"_s));

    failure = transactionFailure(PkExitCancelledPriority, PkErrorTransactionCancelled, {}, {});
    QVERIFY(!failure.canceled);
    QCOMPARE(failure.error, failureReason(PkExitCancelledPriority, {}, {}));
}

QTEST_GUILESS_MAIN(OfficeInstallerTest)

#include "test_office_installer.moc"
