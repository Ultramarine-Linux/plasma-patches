// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// Tests for the office suite installer's suite list and commands (office-installer/).

#include <QTest>

#include "suites.h"

using namespace Qt::StringLiterals;
using namespace OfficeInstaller;

namespace {
    Suite suiteWithFlatpak(const QString &flatpakId) {
        const QList<Suite> all = suites();
        for (const Suite &suite: all) {
            if (suite.flatpakId == flatpakId) {
                return suite;
            }
        }
        return {};
    }

    class OfficeInstallerTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void recommendsCollaboraFirst();
        static void defaultsToFlatpak();
        static void installCommands();
        static void launchCommands();
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
    // Collabora Office isn't packaged in Fedora or Terra.
    QCOMPARE(sources(suiteWithFlatpak(u"com.collaboraoffice.Office"_s), false), QList{Source::Flatpak});
    QCOMPARE(sources(suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s), false), (QList{Source::Flatpak, Source::Rpm}));
}

void OfficeInstallerTest::installCommands() {
    QCOMPARE(installCommand(suiteWithFlatpak(u"com.collaboraoffice.Office"_s), Source::Flatpak),
             (Command{u"flatpak"_s,
                      {u"install"_s, u"--system"_s, u"--noninteractive"_s, u"-y"_s, u"flathub"_s,
                       u"com.collaboraoffice.Office"_s}}));
    QCOMPARE(installCommand(suiteWithFlatpak(u"org.libreoffice.LibreOffice"_s), Source::Rpm),
             (Command{u"pkcon"_s,
                      {u"install"_s, u"--noninteractive"_s, u"libreoffice-writer"_s, u"libreoffice-calc"_s,
                       u"libreoffice-impress"_s}}));
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
}

QTEST_GUILESS_MAIN(OfficeInstallerTest)

#include "test_office_installer.moc"
