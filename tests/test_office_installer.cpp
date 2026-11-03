// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// tests for the office suite installer's suite list, commands and PackageKit package picking (office-installer/)

#include <QTest>

#include <algorithm>

#include "packagekit.h"
#include "suites.h"

using namespace Qt::StringLiterals;
using namespace OfficeInstaller;

namespace {
    Suite suiteWithFlatpak(const QString &flatpakId) {
        const QList<Suite> all = suites();
        const auto suite = std::ranges::find(all, flatpakId, &Suite::flatpakId);
        return suite == all.end() ? Suite{} : *suite;
    }

    class OfficeInstallerTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void recommendsCollaboraFirst();
        static void defaultsToFlatpak();
        static void flatpakRefs();
        static void launchCommands();
        static void recognizesInstalledSuites();
        static void picksPackagesToInstall();
    };
} // namespace

void OfficeInstallerTest::recommendsCollaboraFirst() {
    const QList<Suite> all = suites();
    QVERIFY(!all.isEmpty());
    QCOMPARE(all.constFirst().flatpakId, u"com.collaboraoffice.Office"_s);
    QVERIFY(all.constFirst().recommended);
    QCOMPARE(std::ranges::count_if(all, &Suite::recommended), 1);
}

void OfficeInstallerTest::defaultsToFlatpak() {
    for (const Suite &suite: suites()) {
        QCOMPARE(sources(suite, false).value(0), Source::Flatpak);
        QCOMPARE(sources(suite, true), QList{Source::Flatpak});
    }
    QCOMPARE(sources(suiteWithFlatpak(u"com.collaboraoffice.Office"_s), false), (QList{Source::Flatpak, Source::Rpm}));
    QCOMPARE(sources(suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s), false), (QList{Source::Flatpak, Source::Rpm}));
}

void OfficeInstallerTest::flatpakRefs() {
    QCOMPARE(flatpakRef(suiteWithFlatpak(u"com.collaboraoffice.Office"_s), u"x86_64"_s),
             u"app/com.collaboraoffice.Office/x86_64/stable"_s);
    QCOMPARE(flatpakRef(suiteWithFlatpak(u"org.onlyoffice.desktopeditors"_s), u"aarch64"_s),
             u"app/org.onlyoffice.desktopeditors/aarch64/stable"_s);
}

void OfficeInstallerTest::launchCommands() {
    const Suite collabora = suiteWithFlatpak(u"com.collaboraoffice.Office"_s);
    QCOMPARE(launchCommand(collabora, Source::Flatpak, {u"/home/a/b c.docx"_s}),
             (Command{u"flatpak"_s,
                      {u"run"_s, u"--file-forwarding"_s, u"com.collaboraoffice.Office"_s, u"@@"_s,
                       u"/home/a/b c.docx"_s, u"@@"_s}}));
    QCOMPARE(launchCommand(collabora, Source::Flatpak, {}),
             (Command{u"flatpak"_s, {u"run"_s, u"com.collaboraoffice.Office"_s}}));
    QCOMPARE(launchCommand(suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s), Source::Rpm, {u"/tmp/x.odt"_s}),
             (Command{u"libreoffice"_s, {u"/tmp/x.odt"_s}}));
    QCOMPARE(launchCommand(collabora, Source::Rpm, {u"/tmp/x.odt"_s}), (Command{u"coda-qt"_s, {u"/tmp/x.odt"_s}}));
}

void OfficeInstallerTest::recognizesInstalledSuites() {
    const Suite libreoffice = suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s);
    const Suite collabora = suiteWithFlatpak(u"com.collaboraoffice.Office"_s);
    const Suite onlyoffice = suiteWithFlatpak(u"org.onlyoffice.desktopeditors"_s);
    // Flatpaks, by desktop file name
    QVERIFY(isSuite(libreoffice, u"org.libreoffice.LibreOffice.writer"_s, u"flatpak"_s));
    QVERIFY(isSuite(onlyoffice, u"org.onlyoffice.desktopeditors"_s, u"flatpak"_s));
    QVERIFY(!isSuite(libreoffice, u"org.libreoffice.LibreOfficeX"_s, u"flatpak"_s));
    QVERIFY(!isSuite(onlyoffice, u"com.collaboraoffice.Office"_s, u"flatpak"_s));
    // RPMs, by executable
    QVERIFY(isSuite(libreoffice, u"libreoffice-writer"_s, u"libreoffice"_s));
    QVERIFY(isSuite(collabora, u"coda"_s, u"coda-qt"_s));
    QVERIFY(!isSuite(collabora, u"libreoffice-writer"_s, u"libreoffice"_s));
    // ONLYOFFICE has no RPM, so an empty executable mustn't match it
    QVERIFY(!isSuite(onlyoffice, u"something"_s, QString()));
}

void OfficeInstallerTest::picksPackagesToInstall() {
    const QList<ResolvedPackage> resolved{
        {.info = 2, .id = u"libreoffice-writer;25.8.1-1.fc44;x86_64;updates"_s},
        {.info = 1, .id = u"libreoffice-calc;25.8.1-1.fc44;x86_64;installed"_s},
        {.info = 2, .id = u"libreoffice-calc;25.8.2-1.fc44;x86_64;updates"_s},
        {.info = 1, .id = u"libreoffice-impress;25.8.1-1.fc44;x86_64;installed"_s},
        // a prefix of another name doesn't count
        {.info = 2, .id = u"libreoffice-writer-extras;1-1;x86_64;fedora"_s},
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

QTEST_GUILESS_MAIN(OfficeInstallerTest)

#include "test_office_installer.moc"
