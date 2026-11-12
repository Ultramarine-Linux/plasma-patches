// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>
#include <unistd.h>

#include "activity.h"
#include "apps.h"
#include "cli.h"
#include "flatpak.h"
#include "image.h"
#include "inhibit.h"
#include "lock.h"
#include "nix.h"
#include "packagekit.h"
#include "plasmoid.h"
#include "rpm.h"
#include "schedule.h"
#include "settings.h"
#include "updates.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace UltramarineUpdates;

namespace {
    FileReader fakeFiles(const QHash<QString, QString> &files) {
        return [files](const QString &path) -> std::optional<QByteArray> {
            if (const auto it = files.constFind(path); it != files.constEnd()) {
                return it->toUtf8();
            }
            return std::nullopt;
        };
    }

    QDateTime referenceTime() { return QDateTime::fromSecsSinceEpoch(1'800'000'000); }

    QDateTime minutesLater(const int minutes) { return referenceTime().addSecs(minutes * 60LL); }

    ActivityTracker trackerIn(const QTemporaryDir &dir) {
        return ActivityTracker(dir.path() + "/inactive-since"_L1, dir.path() + "/last-poll"_L1);
    }

    bool writeFile(const QString &path, const QByteArray &contents) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    class UpdaterTest : public QObject {
        Q_OBJECT

    private Q_SLOTS:
        static void parsesDnfCheckUpdateDnf5();
        static void parseDnfCheckUpdateIgnoresObsoletes();
        static void parsesRpmPackageNames();
        static void parseRpmInstalledPrefersNewestInstall();
        static void parsesTabSizes();
        static void parsesDesktopEntries();
        static void parsesMetainfo();
        static void parsesRpmAppFiles();
        static void resolvesAppsFromMetainfo();
        static void resolveAppFallsBackToLaunchers();
        static void parsesFlatpakRemoteLs();
        static void flatpakUpdateIdsAreUnique();
        static void flatpakKindFromRef();
        static void parsesSISizes();
        static void parsesFlatpakSizes();
        static void parsesNixProfileListV3();
        static void parsesNixProfileListV2();
        static void parsesDrvVersions();
        static void nixUpdateFor();
        static void evaluatesNixUpgradesPerElement();
        static void resolvesNixApps();
        static void parsesDeterminateNixVersions();
        static void parsesImageStatus();
        static void parsesBootcState();
        static void parsesPlasmoidRegistry();
        static void plasmoidUpdate();
        static void settingsRoundTrip();
        static void brokenSettingsFallBackToDefaults();
        static void inWindow();
        static void validatesSettings();
        static void parsesTimerShow();
        static void combinesTonightStatus();
        static void tonightAlreadyInstalled();
        static void escalatedCommandLine();
        static void checksOptions();
        static void applyResultCountsAsInstalled();
        static void installFromTimerRecordsInstalls();
        static void installFromTimerRetriesAfterAnHour();
        static void installFromTimerKeepsPartialInstalls();
        static void installFromTimerSkipsWhileBusy();
        static void notifiesOnlyAfterASuccessfulListing();
        static void listFailsOnlyIfEverySourceFailed();
        static void readyToInstall();
        static void sessionInUse();
        static void timestamps();
        static void activityTracker();
        static void activityTrackerRestartsAfterGaps();
        static void activityTrackerCountsFromIdleHint();
        static void sessionsIdleSince();
        static void idleInhibited();
        static void screenInhibitedMarker();
        static void installLock();
        static void installLockWaits();
        static void parsesBootcStatusCache();
        static void classifiesSystemUpdateTarget();
        static void detectsPendingOfflineUpdate();
        static void parsesOfflineResults();
        static void reportsLastOfflineUpdateError();
        static void parsesOsReleaseVersionId();
        static void parsesPkgcliError();
    };

} // namespace

void UpdaterTest::parsesDnfCheckUpdateDnf5() {
    const QList<RpmUpdate> got = parseDnfCheckUpdate(u"Updating and loading repositories:\n"
                                                     u"Repositories loaded.\n"
                                                     u"Upgrades\n"
                                                     u"1password-cli.x86_64 2.41.1-1 1password\n"
                                                     u"kernel-core.x86_64   6.11.4-300.fc41 updates"_s);
    const QList<RpmUpdate> want{
        {.nameArch = u"1password-cli.x86_64"_s, .version = u"2.41.1-1"_s, .repo = u"1password"_s},
        {.nameArch = u"kernel-core.x86_64"_s, .version = u"6.11.4-300.fc41"_s, .repo = u"updates"_s},
    };
    QCOMPARE(got, want);
    QVERIFY(parseDnfCheckUpdate({}).isEmpty());
}

void UpdaterTest::parseDnfCheckUpdateIgnoresObsoletes() {
    const QList<RpmUpdate> got =
        parseDnfCheckUpdate(u"Last metadata expiration check: 0:01:00 ago on Mon 01 Jan 2024.\n\n"
                            u"foo.noarch                 1.2-3.fc41                 updates\n"
                            u"Obsoleting packages\n"
                            u"bar.x86_64                 2.0-1.fc41                 updates\n"
                            u"    baz.x86_64             1.0-1.fc40                 @System"_s);
    QCOMPARE(got, (QList<RpmUpdate>{{.nameArch = u"foo.noarch"_s, .version = u"1.2-3.fc41"_s, .repo = u"updates"_s}}));
}

void UpdaterTest::parsesRpmPackageNames() {
    QCOMPARE(rpmPackageName(u"python3.12-libs.x86_64"_s), u"python3.12-libs"_s);
}

void UpdaterTest::parseRpmInstalledPrefersNewestInstall() {
    const QHash<QString, QString> got = parseRpmInstalled(u"kernel-core.x86_64\t6.11.4-300.fc41\t200\n"
                                                          u"kernel-core.x86_64\t6.11.5-300.fc41\t300\n"
                                                          u"kernel-core.x86_64\t6.11.3-300.fc41\t100\n"
                                                          u"bash.x86_64\t5.2.32-1.fc41\t50"_s);
    QCOMPARE(got.value(u"kernel-core.x86_64"_s), u"6.11.5-300.fc41"_s);
    QCOMPARE(got.value(u"bash.x86_64"_s), u"5.2.32-1.fc41"_s);
}

void UpdaterTest::parsesTabSizes() {
    const QHash<QString, UpgradeSize> got =
        parseTabSizes(u"libddcutil.x86_64\t289000\t1200000\nbash.x86_64\t8873537\nbroken\n"_s);
    QCOMPARE(got.value(u"libddcutil.x86_64"_s), (UpgradeSize{.download = 289000, .installed = 1200000}));
    QCOMPARE(got.value(u"bash.x86_64"_s).download, 8873537);
    QVERIFY(!got.contains(u"broken"_s));
}

void UpdaterTest::parsesDesktopEntries() {
    const std::optional<DesktopEntry> entry = parseDesktopEntry(u"[Desktop Entry]\n"
                                                                u"Type=Application\n"
                                                                u"Name=LibreOffice Writer\n"
                                                                u"Name[de]=LibreOffice Writer DE\n"
                                                                u"Icon=libreoffice-writer\n\n"
                                                                u"[Desktop Action new]\n"
                                                                u"Name=New Document\n"
                                                                u"Icon=other"_s);
    QVERIFY(entry);
    QCOMPARE(*entry, (DesktopEntry{.name = u"LibreOffice Writer"_s, .icon = u"libreoffice-writer"_s}));

    for (const QString &hidden: {
             u"[Desktop Entry]\nType=Application\nName=X\nNoDisplay=true"_s,
             u"[Desktop Entry]\nType=Application\nName=X\nHidden=true"_s,
             u"[Desktop Entry]\nType=Link\nName=X"_s,
             u"[Desktop Entry]\nType=Application\nIcon=x"_s,
         }) {
        QVERIFY2(!parseDesktopEntry(hidden), qPrintable(hidden));
    }
}

void UpdaterTest::parsesMetainfo() {
    std::optional<Metainfo> info = parseMetainfo(R"(<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>org.mozilla.firefox</id>
  <name xml:lang="de">Feuerfuchs</name>
  <name>Firefox</name>
  <icon type="remote">https://example.org/firefox.png</icon>
  <icon type="stock">firefox</icon>
  <launchable type="desktop-id">org.mozilla.firefox.desktop</launchable>
  <provides><id>firefox.desktop</id></provides>
</component>)");
    QVERIFY(info);
    QCOMPARE(*info,
             (Metainfo{.id = u"org.mozilla.firefox"_s,
                       .name = u"Firefox"_s,
                       .icon = u"firefox"_s,
                       .desktopIds = {u"org.mozilla.firefox.desktop"_s}}));

    info =
        parseMetainfo(R"(<component type="desktop"><id>org.kde.dolphin.desktop</id><name>Dolphin</name></component>)");
    QVERIFY(info);
    QCOMPARE(info->name, u"Dolphin"_s);
    QCOMPARE(info->desktopIds, QStringList{u"org.kde.dolphin.desktop"_s});

    for (const QByteArray &notApp: {
             QByteArray(
                 R"(<component type="console-application"><id>dev.linderud.sbctl</id><name>sbctl</name></component>)"),
             QByteArray(R"(<component type="addon"><id>org.kde.dolphin.plugin</id><name>Plugin</name></component>)"),
             QByteArray(R"(<component type="desktop-application"><id>x</id></component>)"),
             QByteArray("not xml"),
         }) {
        QVERIFY2(!parseMetainfo(notApp), notApp.constData());
    }
}

void UpdaterTest::parsesRpmAppFiles() {
    const QHash<QString, AppFiles> got =
        parseRpmAppFiles(u"firefox.x86_64\t/usr/bin/firefox\n"
                         u"firefox.x86_64\t/usr/share/metainfo/org.mozilla.firefox.appdata.xml\n"
                         u"firefox.x86_64\t/usr/share/applications/org.mozilla.firefox.desktop\n"
                         u"old.x86_64\t/usr/share/appdata/old.appdata.xml\n"
                         u"foo.x86_64\t/usr/share/applications/sub/nested.desktop\n"
                         u"foo.x86_64\t/usr/share/applications/foo.png"_s);
    const QHash<QString, AppFiles> want{
        {u"firefox.x86_64"_s,
         {.metainfo = {u"/usr/share/metainfo/org.mozilla.firefox.appdata.xml"_s},
          .desktop = {u"/usr/share/applications/org.mozilla.firefox.desktop"_s},
          .applicationsDir = {}}},
        {u"old.x86_64"_s,
         {.metainfo = {u"/usr/share/appdata/old.appdata.xml"_s}, .desktop = {}, .applicationsDir = {}}},
    };
    QCOMPARE(got, want);
}

void UpdaterTest::resolvesAppsFromMetainfo() {
    const FileReader read = fakeFiles({
        {u"/usr/share/metainfo/org.kde.dolphin.appdata.xml"_s,
         uR"(<component type="desktop"><id>org.kde.dolphin.desktop</id><name>Dolphin</name></component>)"_s},
        {u"/usr/share/metainfo/org.kde.dolphin.plugin.xml"_s,
         uR"(<component type="addon"><id>org.kde.dolphin.plugin</id><name>Plugin</name></component>)"_s},
        {u"/usr/share/applications/org.kde.dolphin.desktop"_s,
         u"[Desktop Entry]\nType=Application\nName=Dolphin (launcher)\nIcon=system-file-manager"_s},
        {u"/usr/share/metainfo/dev.linderud.sbctl.metainfo.xml"_s,
         uR"(<component type="console-application"><id>dev.linderud.sbctl</id><name>sbctl</name></component>)"_s},
        {u"/usr/share/applications/sbctl-helper.desktop"_s, u"[Desktop Entry]\nType=Application\nName=Helper"_s},
    });

    const AppFiles dolphin{.metainfo = {u"/usr/share/metainfo/org.kde.dolphin.plugin.xml"_s,
                                        u"/usr/share/metainfo/org.kde.dolphin.appdata.xml"_s},
                           .desktop = {},
                           .applicationsDir = {}};
    const std::optional<AppInfo> app = resolveApp(u"dolphin"_s, &dolphin, read);
    QVERIFY(app);
    QCOMPARE(*app,
             (AppInfo{.id = u"org.kde.dolphin.desktop"_s, .name = u"Dolphin"_s, .icon = u"system-file-manager"_s}));

    const AppFiles sbctl{.metainfo = {u"/usr/share/metainfo/dev.linderud.sbctl.metainfo.xml"_s},
                         .desktop = {u"/usr/share/applications/sbctl-helper.desktop"_s},
                         .applicationsDir = {}};
    QVERIFY(!resolveApp(u"sbctl"_s, &sbctl, read));
}

void UpdaterTest::resolveAppFallsBackToLaunchers() {
    const FileReader read = fakeFiles({
        {u"/usr/share/applications/a-helper.desktop"_s, u"[Desktop Entry]\nType=Application\nName=Helper\nIcon=h"_s},
        {u"/usr/share/applications/hidden.desktop"_s,
         u"[Desktop Entry]\nType=Application\nName=Hidden\nNoDisplay=true"_s},
        {u"/usr/share/applications/1password.desktop"_s,
         u"[Desktop Entry]\nType=Application\nName=1Password\nIcon=1password"_s},
    });
    const AppFiles files{
        .metainfo = {},
        .desktop = {u"/usr/share/applications/a-helper.desktop"_s, u"/usr/share/applications/hidden.desktop"_s,
                    u"/usr/share/applications/1password.desktop"_s, u"/usr/share/applications/gone.desktop"_s},
        .applicationsDir = {}};

    std::optional<AppInfo> app = resolveApp(u"1password"_s, &files, read);
    QVERIFY(app);
    QCOMPARE(*app, (AppInfo{.id = {}, .name = u"1Password"_s, .icon = u"1password"_s}));
    app = resolveApp(u"other"_s, &files, read);
    QVERIFY(app);
    QCOMPARE(app->name, u"Helper"_s);

    const AppFiles hidden{
        .metainfo = {}, .desktop = {u"/usr/share/applications/hidden.desktop"_s}, .applicationsDir = {}};
    QVERIFY(!resolveApp(u"x"_s, &hidden, read));
    QVERIFY(!resolveApp(u"x"_s, nullptr, read));
}

void UpdaterTest::parsesFlatpakRemoteLs() {
    const QList<FlatpakUpdate> got =
        parseFlatpakRemoteLs(u"org.kde.haruna\tHaruna\t1.8.1\tstable\tflathub\n"
                             u"org.freedesktop.Platform.GL.default\tMesa\t26.2.2\t25.08\tflathub\n"
                             u"short\tline"_s);
    QCOMPARE(got.size(), 2);
    QCOMPARE(got[0],
             (FlatpakUpdate{.appId = u"org.kde.haruna"_s,
                            .name = u"Haruna"_s,
                            .version = u"1.8.1"_s,
                            .branch = u"stable"_s,
                            .origin = u"flathub"_s,
                            .ref = {}}));

    const QList<FlatpakUpdate> emptyVersion = parseFlatpakRemoteLs(
        u"org.freedesktop.Platform.codecs-extra\tCodecs Extra Extension\t\t25.08-extra\tflathub"_s);
    QCOMPARE(emptyVersion.size(), 1);
    QVERIFY(emptyVersion[0].version.isEmpty());
    QCOMPARE(emptyVersion[0].branch, u"25.08-extra"_s);
}

void UpdaterTest::flatpakUpdateIdsAreUnique() {
    const QHash<QString, QString> installed{{u"org.freedesktop.Platform.GL.default/25.08"_s, u"26.2.1"_s}};
    const auto mesa = [](const QString &branch, const QString &ref) {
        return FlatpakUpdate{.appId = u"org.freedesktop.Platform.GL.default"_s,
                             .name = u"Mesa"_s,
                             .version = u"26.2.2"_s,
                             .branch = branch,
                             .origin = u"flathub"_s,
                             .ref = ref};
    };
    const PackageUpdate a = flatpakPackageUpdate(
        mesa(u"25.08"_s, u"runtime/org.freedesktop.Platform.GL.default/x86_64/25.08"_s), u"system"_s, installed);
    const PackageUpdate b = flatpakPackageUpdate(mesa(u"24.08"_s, {}), u"system"_s, installed);
    const PackageUpdate c = flatpakPackageUpdate(mesa(u"25.08"_s, {}), u"user"_s, {});
    QVERIFY(a.id != b.id);
    QVERIFY(a.id != c.id);
    QCOMPARE(a.oldVersion, u"26.2.1"_s);
    QCOMPARE(a.icon, u"org.freedesktop.Platform.GL.default"_s);
    QCOMPARE(a.source, u"Flatpak (flathub)"_s);
    QCOMPARE(c.source, u"Flatpak (flathub) [user]"_s);
}

void UpdaterTest::flatpakKindFromRef() {
    const QList<FlatpakUpdate> got =
        parseFlatpakRemoteLs(u"org.kde.haruna\tHaruna\t1.8.1\tstable\tflathub\tapp/org.kde.haruna/x86_64/stable\n"
                             u"org.gnome.Platform\tGNOME\t\t51\tflathub\truntime/org.gnome.Platform/x86_64/51"_s);
    QCOMPARE(got.size(), 2);
    const PackageUpdate app = flatpakPackageUpdate(got[0], u"system"_s, {});
    const PackageUpdate runtime = flatpakPackageUpdate(got[1], u"system"_s, {});
    QCOMPARE(app.kind, u"app"_s);
    QCOMPARE(app.appId, u"org.kde.haruna"_s);
    QCOMPARE(app.backend, u"flatpak"_s);
    QCOMPARE(runtime.kind, u"runtime"_s);
    QVERIFY(runtime.appId.isEmpty());
}

void UpdaterTest::parsesSISizes() {
    const QList<std::pair<QString, qint64>> cases{
        {u"418.7 MB"_s, 418700000},
        {u"431.7\u00a0MB"_s, 431700000},
        {u"1.1 GB"_s, 1100000000},
        {u"12.0 kB"_s, 12000},
        {u"512 bytes"_s, 512},
        {u"1 byte"_s, 1},
        {QString(), 0},
        {u"1,1 GB"_s, 0},
        {u"3 parsecs"_s, 0},
    };
    for (const auto &[input, want]: cases) {
        QCOMPARE(parseSISize(input), want);
    }
}

void UpdaterTest::parsesFlatpakSizes() {
    const QHash<QString, UpgradeSize> got =
        parseFlatpakSizes(u"org.gnome.Platform\t51\t431.7 MB\t1.1 GB\napp.zen_browser.zen\tstable\t435.4 MB\n"_s);
    QCOMPARE(got.value(u"org.gnome.Platform/51"_s), (UpgradeSize{.download = 431700000, .installed = 1100000000}));
    QCOMPARE(got.value(u"app.zen_browser.zen/stable"_s).download, 435400000);
}

void UpdaterTest::parsesNixProfileListV3() {
    const std::optional<QList<NixProfileElement>> got = parseNixProfileList(R"({"elements":{
        "flox":{"active":true,"attrPath":"packages.x86_64-linux.default","originalUrl":"github:flox/flox/latest","outputs":null,"priority":5,"storePaths":["/nix/store/rdyh48k2s5919hpc45ifkczws3668k2s-flox-1.17.0-g486737b"],"url":"github:flox/flox/486737b"},
        "local":{"active":true,"attrPath":"","originalUrl":"","storePaths":["/nix/store/x-local-1"]},
        "off":{"active":false,"attrPath":"legacyPackages.x86_64-linux.hello","originalUrl":"flake:nixpkgs","storePaths":["/nix/store/y-hello-2.12"]},
        "btop":{"attrPath":"legacyPackages.x86_64-linux.btop","originalUrl":"flake:nixpkgs","outputs":["out"],"storePaths":["/nix/store/z-btop-1.4.0"]}
    },"version":3})",
                                                                            nullptr);
    QVERIFY(got);
    QCOMPARE(got->size(), 2);
    QCOMPARE(got->at(0).name, u"btop"_s);
    QCOMPARE(got->at(1).name, u"flox"_s);
    QCOMPARE(got->at(0).outputs, std::optional<QStringList>({u"out"_s}));
    QVERIFY(!got->at(1).outputs);
}

void UpdaterTest::parsesNixProfileListV2() {
    std::optional<QList<NixProfileElement>> got = parseNixProfileList(
        R"({"elements":[{"active":true,"attrPath":"legacyPackages.x86_64-linux.hello","originalUrl":"flake:nixpkgs","storePaths":["/nix/store/y-hello-2.12"]}],"version":2})",
        nullptr);
    QVERIFY(got);
    QCOMPARE(got->size(), 1);
    QCOMPARE(got->constFirst().name, u"hello"_s);

    got = parseNixProfileList(R"({"elements":{},"version":3})", nullptr);
    QVERIFY(got && got->isEmpty());

    QString error;
    QVERIFY(!parseNixProfileList("not json", &error));
    QVERIFY(!error.isEmpty());
}

void UpdaterTest::parsesDrvVersions() {
    const QList<std::pair<QString, QString>> cases{
        {u"flox-1.17.0-g486737b"_s, u"1.17.0-g486737b"_s},
        {u"hello-2.12.1"_s, u"2.12.1"_s},
        {u"nix-index-unstable"_s, {}},
        {u"python3.12-foo-1.0"_s, u"1.0"_s},
        {u"noversion"_s, {}},
    };
    for (const auto &[name, want]: cases) {
        QCOMPARE(drvVersion(name), want);
    }
    QCOMPARE(storePathName(u"/nix/store/rdyh48k2s5919hpc45ifkczws3668k2s-flox-1.17.0"_s), u"flox-1.17.0"_s);
}

void UpdaterTest::nixUpdateFor() {
    NixProfileElement element;
    element.name = u"flox"_s;
    element.originalUrl = u"github:flox/flox/latest"_s;
    element.storePaths = {u"/nix/store/a-flox-1.17.0-g486737b"_s};

    std::optional<PackageUpdate> update =
        UltramarineUpdates::nixUpdateFor(element,
                                         {.error = false,
                                          .name = u"flox-1.19.0-ga6a3147"_s,
                                          .version = u"1.19.0-ga6a3147"_s,
                                          .outPaths = {u"/nix/store/b-flox-1.19.0-ga6a3147"_s}});
    QVERIFY(update);
    PackageUpdate want;
    want.id = u"nix:flox"_s;
    want.name = u"flox"_s;
    want.source = u"Nix (github:flox/flox/latest)"_s;
    want.backend = u"nix"_s;
    want.kind = u"package"_s;
    want.icon = u"package-x-generic"_s;
    want.oldVersion = u"1.17.0-g486737b"_s;
    want.newVersion = u"1.19.0-ga6a3147"_s;
    QCOMPARE(*update, want);

    QVERIFY(!UltramarineUpdates::nixUpdateFor(
        element, {.error = false, .name = {}, .version = {}, .outPaths = {u"/nix/store/a-flox-1.17.0-g486737b"_s}}));
    update = UltramarineUpdates::nixUpdateFor(element,
                                              {.error = false,
                                               .name = u"flox-1.17.0-g486737b"_s,
                                               .version = {},
                                               .outPaths = {u"/nix/store/c-flox-1.17.0-g486737b"_s}});
    QVERIFY(update);
    QCOMPARE(update->newVersion, u"1.17.0-g486737b"_s);
    QVERIFY(!UltramarineUpdates::nixUpdateFor(element, {.error = true, .name = {}, .version = {}, .outPaths = {}}));
}

void UpdaterTest::evaluatesNixUpgradesPerElement() {
    if (nixBinary().isEmpty()) {
        QSKIP("nix isn't installed");
    }
    const QTemporaryDir dir;
    QVERIFY(writeFile(dir.path() + "/flake.nix"_L1, R"nix({
  outputs = { self }: {
    good = derivation { name = "good-1.0"; system = "x86_64-linux"; builder = "/bin/sh"; outputs = [ "out" "dev" ]; };
    unfree = { type = "derivation"; name = "unfree-1.0"; out = throw "unfree"; };
  };
})nix"));
    const auto element = [&dir](const QString &attrPath, const std::optional<QStringList> &outputs = std::nullopt) {
        NixProfileElement e;
        e.name = attrPath;
        e.attrPath = attrPath;
        e.originalUrl = "path:"_L1 + dir.path();
        e.outputs = outputs;
        return e;
    };

    QString error;
    const std::optional<QList<NixEvalResult>> probe = evaluateNixUpgrades({element(u"good"_s)}, &error);
    if (!probe) {
        QSKIP(qPrintable(u"nix can't evaluate flakes here: "_s + error));
    }
    QCOMPARE(probe->size(), 1);
    QVERIFY(!probe->at(0).error);
    QCOMPARE(probe->at(0).name, u"good-1.0"_s);
    QCOMPARE(probe->at(0).outPaths.size(), 1);

    const std::optional<QList<NixEvalResult>> results =
        evaluateNixUpgrades({element(u"good"_s, QStringList{u"out"_s, u"man"_s}), element(u"unfree"_s),
                             element(u"missing"_s), element(u"good"_s, QStringList{u"out"_s, u"dev"_s})},
                            &error);
    QVERIFY2(results, qPrintable(error));
    QCOMPARE(results->size(), 4);
    QVERIFY(!results->at(0).error);
    QCOMPARE(results->at(0).outPaths, probe->at(0).outPaths);
    QVERIFY(results->at(1).error);
    QVERIFY(results->at(2).error);
    QVERIFY(!results->at(3).error);
    QCOMPARE(results->at(3).outPaths.size(), 2);
}

void UpdaterTest::resolvesNixApps() {
    const QString store = u"/nix/store/h-warp-7.2"_s;
    const FileReader read = fakeFiles({
        {store + "/share/metainfo/dev.warp.Warp.metainfo.xml"_L1,
         uR"(<component type="desktop-application"><id>dev.warp.Warp</id><name>Warp</name><launchable type="desktop-id">dev.warp.Warp.desktop</launchable></component>)"_s},
        {store + "/share/applications/dev.warp.Warp.desktop"_L1,
         u"[Desktop Entry]\nType=Application\nName=Warp\nIcon=dev.warp.Warp"_s},
    });
    const AppFiles files{.metainfo = {store + "/share/metainfo/dev.warp.Warp.metainfo.xml"_L1},
                         .desktop = {store + "/share/applications/dev.warp.Warp.desktop"_L1},
                         .applicationsDir = store + "/share/applications"_L1};
    const std::optional<AppInfo> app = resolveApp(u"warp"_s, &files, read);
    QVERIFY(app);
    QCOMPARE(*app, (AppInfo{.id = u"dev.warp.Warp"_s, .name = u"Warp"_s, .icon = u"dev.warp.Warp"_s}));
}

void UpdaterTest::parsesDeterminateNixVersions() {
    auto [installed, latest] = determinateNixVersions(
        u"Determinate Nixd daemon version: 3.22.5\nDeterminate Nixd client version: 3.22.5\nLatest version: 3.23.3\n\n"
        u"A new version of Determinate Nix is available.\n"_s);
    QCOMPARE(installed, u"3.22.5"_s);
    QCOMPARE(latest, u"3.23.3"_s);
    std::tie(installed, latest) = determinateNixVersions(u"Determinate Nixd client version: 3.22.5\n"_s);
    QCOMPARE(installed, u"3.22.5"_s);
    QVERIFY(latest.isEmpty());

    QVERIFY(determinateNixUpgradable(u"3.22.5"_s, u"3.23.3"_s));
    QVERIFY(determinateNixUpgradable(u"3.9.0"_s, u"3.10.0"_s));
    QVERIFY(!determinateNixUpgradable(u"3.23.3"_s, u"3.22.5"_s));
    QVERIFY(!determinateNixUpgradable(u"3.22.5"_s, u"3.22.5"_s));
    QVERIFY(!determinateNixUpgradable(u"3.22.5"_s, {}));
    QVERIFY(!determinateNixUpgradable({}, u"3.22.5"_s));
}

void UpdaterTest::parsesImageStatus() {
    std::optional<BootcStatus> bootc = parseBootcStatus(
        R"({"spec":{"image":{"image":"ghcr.io/ultramarine-linux/plasma-bootc:44","transport":"registry"}}})");
    QVERIFY(bootc);
    QCOMPARE(bootc->image, u"ghcr.io/ultramarine-linux/plasma-bootc:44"_s);
    QVERIFY(!isUmEnvImage(bootc->image));
    QVERIFY(isUmEnvImage(u"localhost/um-env"_s));
    QVERIFY(isUmEnvImage(u"localhost/um-env:latest"_s));
    bootc = parseBootcStatus(R"({"spec":{"image":null}})");
    QVERIFY(bootc && bootc->image.isEmpty());

    const QByteArray status = R"({"deployments":[
        {"booted":false,"checksum":"old","version":"44.20260901.0"},
        {"booted":true,"checksum":"abc","version":"44.20261001.0","container-image-reference":"ostree-image-signed:docker://ghcr.io/ultramarine-linux/plasma-bootc:44"}],
        "cached-update":{"checksum":"def","version":"44.20261020.0"}})";
    ImageState state = parseRpmOstreeState(status, u"Ultramarine Linux"_s, u"bootc"_s);
    QVERIFY(state.update);
    QVERIFY(!state.staged);
    QCOMPARE(state.update->oldVersion, u"44.20261001.0"_s);
    QCOMPARE(state.update->newVersion, u"44.20261020.0"_s);
    QCOMPARE(state.update->backend, u"bootc"_s);
    QCOMPARE(state.update->source, u"Image (ostree-image-signed:docker://ghcr.io/ultramarine-linux/plasma-bootc:44)"_s);

    QVERIFY(!parseRpmOstreeState(R"({"deployments":[{"booted":true,"checksum":"abc"}],"cached-update":null})", {},
                                 u"bootc"_s)
                 .update);
    QVERIFY(
        !parseRpmOstreeState(R"({"deployments":[{"booted":true,"checksum":"abc"}],"cached-update":{"checksum":"abc"}})",
                             {}, u"bootc"_s)
             .update);

    state = parseRpmOstreeState(R"({"deployments":[
        {"booted":false,"staged":true,"checksum":"def","version":"44.20261020.0"},
        {"booted":true,"checksum":"abc","version":"44.20261001.0"}],
        "cached-update":{"checksum":"def","version":"44.20261020.0"}})",
                                {}, u"rpm-ostree"_s);
    QVERIFY(!state.update);
    QVERIFY(state.staged);
    QCOMPARE(state.stagedVersion, u"44.20261020.0"_s);
}

void UpdaterTest::parsesBootcState() {
    // abridged `bootc status --format=json`, where cachedUpdate is per boot entry
    const auto host = [](const QString &staged, const QString &cachedUpdate) {
        return uR"({"apiVersion":"org.containers.bootc/v1","kind":"BootcHost",
            "spec":{"image":{"image":"ghcr.io/ultramarine-linux/plasma-bootc:44","transport":"registry"}},
            "status":{"staged":%1,"booted":{
                "image":{"image":{"image":"ghcr.io/ultramarine-linux/plasma-bootc:44","transport":"registry"},
                         "version":"44.20261001.0","imageDigest":"sha256:aaaa"},
                "cachedUpdate":%2,"downloadOnly":false},"rollback":null}})"_s.arg(staged, cachedUpdate)
            .toUtf8();
    };
    const QString newer = uR"({"image":{"image":"ghcr.io/ultramarine-linux/plasma-bootc:44","transport":"registry"},
        "version":"44.20261020.0","imageDigest":"sha256:bbbb"})"_s;
    const auto stagedEntry = [&newer](const bool downloadOnly) {
        return uR"({"image":%1,"cachedUpdate":null,"downloadOnly":%2})"_s.arg(newer,
                                                                              downloadOnly ? u"true"_s : u"false"_s);
    };

    ImageState state = parseBootcState(host(u"null"_s, newer), u"Ultramarine Linux"_s);
    QVERIFY(state.update);
    QVERIFY(!state.staged);
    QCOMPARE(state.update->name, u"Ultramarine Linux"_s);
    QCOMPARE(state.update->backend, u"bootc"_s);
    QCOMPARE(state.update->oldVersion, u"44.20261001.0"_s);
    QCOMPARE(state.update->newVersion, u"44.20261020.0"_s);
    QCOMPARE(state.update->source, u"Image (ghcr.io/ultramarine-linux/plasma-bootc:44)"_s);

    state = parseBootcState(host(u"null"_s, u"null"_s), {});
    QVERIFY(!state.update);
    QVERIFY(!state.staged);

    state = parseBootcState(host(stagedEntry(false), newer), {});
    QVERIFY(!state.update);
    QVERIFY(state.staged);
    QCOMPARE(state.stagedVersion, u"44.20261020.0"_s);

    state = parseBootcState(host(stagedEntry(true), newer), {});
    QVERIFY(state.update);
    QVERIFY(!state.staged);

    state = parseBootcState(host(stagedEntry(false), uR"({"image":{"image":"x","transport":"registry"},
        "version":"44.20261030.0","imageDigest":"sha256:cccc"})"_s),
                            {});
    QVERIFY(state.update);
    QCOMPARE(state.update->newVersion, u"44.20261030.0"_s);
    QVERIFY(state.staged);

    state = parseBootcState(host(u"null"_s, uR"({"image":{"image":"localhost/um-env","transport":"containers-storage"},
        "imageDigest":"sha256:0123456789abcdef"})"_s),
                            {});
    QVERIFY(state.update);
    QCOMPARE(state.update->newVersion, u"0123456789ab"_s);
}

void UpdaterTest::parsesPlasmoidRegistry() {
    QVERIFY(registryHasInstalledEntries(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE khotnewstuff3>
<hotnewstuffregistry>
 <stuff category="706">
  <name>Panel Colorizer</name>
  <providerid>api.kde-look.org</providerid>
  <version>v8.0.0</version>
  <installedfile>/home/user/.local/share/plasma/plasmoids/luisbocanegra.panel.colorizer/</installedfile>
  <id>2130967</id>
  <status>installed</status>
 </stuff>
</hotnewstuffregistry>)"));
    QVERIFY(registryHasInstalledEntries(
        "<hotnewstuffregistry><stuff><status>deleted</status></stuff><stuff><status>updateable</status></stuff>"
        "</hotnewstuffregistry>"));
    QVERIFY(!registryHasInstalledEntries("<hotnewstuffregistry/>"));
    QVERIFY(!registryHasInstalledEntries("<hotnewstuffregistry><stuff><status>deleted</status></stuff>"
                                         "</hotnewstuffregistry>"));
    QVERIFY(!registryHasInstalledEntries("not xml"));
}

void UpdaterTest::plasmoidUpdate() {
    const PlasmoidEntry entry{
        .uniqueId = u"2130967"_s,
        .name = u"Panel Colorizer"_s,
        .version = u"v7.0.0"_s,
        .updateVersion = u"v8.0.0"_s,
        .installedFiles = {u"/home/user/.local/share/plasma/plasmoids/luisbocanegra.panel.colorizer/"_s},
    };
    const FileReader files = fakeFiles({
        {u"/home/user/.local/share/plasma/plasmoids/luisbocanegra.panel.colorizer/metadata.json"_s,
         uR"({"KPlugin":{"Id":"luisbocanegra.panel.colorizer","Icon":"desktop","Name":"Panel Colorizer"}})"_s},
    });
    PackageUpdate update = plasmoidPackageUpdate(entry, files);
    QCOMPARE(update.id, u"plasmoid:2130967"_s);
    QCOMPARE(update.name, u"Panel Colorizer"_s);
    QCOMPARE(update.backend, u"plasmoid"_s);
    QCOMPARE(update.source, u"KDE Store"_s);
    QCOMPARE(update.kind, QString::fromLatin1(Kind::Addon));
    QCOMPARE(update.oldVersion, u"v7.0.0"_s);
    QCOMPARE(update.newVersion, u"v8.0.0"_s);
    QCOMPARE(update.icon, u"desktop"_s);

    update = plasmoidPackageUpdate(entry, fakeFiles({}));
    QCOMPARE(update.icon, u"plasma"_s);

    const PlasmoidMetadata metadata = parsePlasmoidMetadata(R"({"KPlugin":{"Id":"org.example.clock"}})");
    QCOMPARE(metadata.id, u"org.example.clock"_s);
    QVERIFY(metadata.icon.isEmpty());
}

void UpdaterTest::settingsRoundTrip() {
    const QTemporaryDir dir;
    const QString path = dir.path() + "/updates/settings.json"_L1;
    qputenv("ULTRAMARINE_UPDATES_CONFIG", path.toLocal8Bit());

    Settings got;
    QString error;
    QVERIFY(loadSettings(&got, &error));
    QCOMPARE(got, Settings{});

    const Settings want{
        .automatic = false, .offline = true, .inactiveMinutes = 10, .windowStart = u"01:00"_s, .windowEnd = u"05:00"_s};
    QVERIFY2(saveSettings(want, &error), qPrintable(error));
    QVERIFY(loadSettings(&got, &error));
    QCOMPARE(got, want);
    QVERIFY(QFile::permissions(path) & QFile::ReadOther);

    QVERIFY(writeFile(path, R"({"offline": true})"));
    QVERIFY(loadSettings(&got, &error));
    QCOMPARE(got,
             (Settings{.automatic = true, .offline = true, .inactiveMinutes = 5, .windowStart = {}, .windowEnd = {}}));

    QVERIFY(writeFile(path, "nope"));
    QVERIFY(!loadSettings(&got, &error));
    QCOMPARE(got, Settings{});

    QVERIFY(!parseSettings(R"({"automatic":"yes"})", &got, &error));
    QVERIFY(!parseSettings(R"({"inactive_minutes":1.5})", &got, &error));
    QVERIFY(!parseSettings(R"({"inactive_minutes":1e300})", &got, &error));
    QVERIFY(!parseSettings(R"({"inactive_minutes":-1})", &got, &error));
    qunsetenv("ULTRAMARINE_UPDATES_CONFIG");
}

void UpdaterTest::brokenSettingsFallBackToDefaults() {
    const QTemporaryDir dir;
    const QString path = dir.path() + "/settings.json"_L1;
    qputenv("ULTRAMARINE_UPDATES_CONFIG", path.toLocal8Bit());

    QString warning = u"stale"_s;
    QCOMPARE(loadSettingsOrDefaults(&warning), Settings{});
    QVERIFY(warning.isEmpty());

    QVERIFY(writeFile(path, R"({"automatic": false, "inactive_minutes": 30})"));
    QCOMPARE(loadSettingsOrDefaults(&warning).inactiveMinutes, 30);
    QVERIFY(warning.isEmpty());

    for (const QByteArray &broken: {QByteArray("{\"automatic\": tru"), QByteArray(R"({"automatic": "no"})")}) {
        QVERIFY(writeFile(path, broken));
        QCOMPARE(loadSettingsOrDefaults(&warning), Settings{});
        QVERIFY(!warning.isEmpty());
    }
    qunsetenv("ULTRAMARINE_UPDATES_CONFIG");
}

void UpdaterTest::inWindow() {
    const auto at = [](const int hour, const int minute) {
        return QDateTime(QDate(2026, 10, 22), QTime(hour, minute));
    };
    QVERIFY(UltramarineUpdates::inWindow(at(3, 0), {}, {}));
    QVERIFY(UltramarineUpdates::inWindow(at(3, 0), u"01:00"_s, u"05:00"_s));
    QVERIFY(!UltramarineUpdates::inWindow(at(5, 0), u"01:00"_s, u"05:00"_s));
    QVERIFY(!UltramarineUpdates::inWindow(at(0, 59), u"01:00"_s, u"05:00"_s));
    QVERIFY(UltramarineUpdates::inWindow(at(23, 30), u"22:00"_s, u"06:00"_s));
    QVERIFY(UltramarineUpdates::inWindow(at(5, 59), u"22:00"_s, u"06:00"_s));
    QVERIFY(!UltramarineUpdates::inWindow(at(12, 0), u"22:00"_s, u"06:00"_s));
}

void UpdaterTest::validatesSettings() {
    const auto valid = [](const int minutes, const QString &start, const QString &end) {
        return Settings{
            .automatic = true, .offline = false, .inactiveMinutes = minutes, .windowStart = start, .windowEnd = end}
            .validate()
            .isEmpty();
    };
    QVERIFY(!valid(-1, {}, {}));
    QVERIFY(!valid(30, u"01:00"_s, {}));
    QVERIFY(!valid(30, u"25:00"_s, u"05:00"_s));
    QVERIFY(valid(30, u"22:00"_s, u"06:00"_s));
    QVERIFY(!valid(30, u"01:00"_s, u"01:00"_s));
    QVERIFY(!valid(30, u"1:00"_s, u"01:00"_s));

    QString start;
    QString end;
    QString error;
    QVERIFY(parseWindow(u" 01:00 - 05:00 "_s, &start, &end, &error));
    QCOMPARE(start, u"01:00"_s);
    QCOMPARE(end, u"05:00"_s);
    QVERIFY(parseWindow({}, &start, &end, &error));
    QVERIFY(start.isEmpty() && end.isEmpty());
    QVERIFY(!parseWindow(u"nope"_s, &start, &end, &error));
}

void UpdaterTest::parsesTimerShow() {
    QCOMPARE(parseTimerShow(u"ActiveState=active\nNextElapseUSecRealtime=@1761181200\n"_s),
             (ScheduleStatus{.scheduled = true, .next = 1761181200}));
    QCOMPARE(parseTimerShow(u"ActiveState=inactive\nNextElapseUSecRealtime=\n"_s), ScheduleStatus{});
}

void UpdaterTest::combinesTonightStatus() {
    const ScheduleStatus system{.scheduled = true, .next = 1761181200};
    const ScheduleStatus user{.scheduled = true, .next = 1761181260};
    const ScheduleStatus none;
    QCOMPARE(combineTonightStatus(system, std::nullopt), system);
    QCOMPARE(combineTonightStatus(system, user), (ScheduleStatus{.scheduled = true, .next = 1761181200}));
    QCOMPARE(combineTonightStatus(system, none), (ScheduleStatus{.partial = true}));
    QCOMPARE(combineTonightStatus(none, user), (ScheduleStatus{.partial = true}));
    QCOMPARE(combineTonightStatus(none, none), ScheduleStatus{});

    const QJsonObject json = ScheduleStatus{.partial = true}.toJson();
    QCOMPARE(json.value(u"scheduled"_s).toBool(true), false);
    QVERIFY(json.value(u"partial"_s).toBool());
    QVERIFY(!ScheduleStatus{.scheduled = true}.toJson().contains(u"partial"_s));
}

void UpdaterTest::tonightAlreadyInstalled() {
    const QDateTime scheduled(QDate(2026, 10, 22), QTime(14, 0));
    QVERIFY(UltramarineUpdates::tonightAlreadyInstalled(scheduled.addSecs(9LL * 3600), scheduled));
    QVERIFY(!UltramarineUpdates::tonightAlreadyInstalled(scheduled.addSecs(-3600), scheduled));
    QVERIFY(!UltramarineUpdates::tonightAlreadyInstalled(std::nullopt, scheduled));
    QVERIFY(!UltramarineUpdates::tonightAlreadyInstalled(scheduled, std::nullopt));
}

void UpdaterTest::listFailsOnlyIfEverySourceFailed() {
    ListResponse response;
    QVERIFY(!response.allFailed());
    response.queriedSources = 3;
    QVERIFY(!response.allFailed());
    response.errors = {u"flatpak user: no network"_s};
    QVERIFY(!response.allFailed());
    response.errors << u"rpm: no network"_s << u"flatpak system: no network"_s;
    QVERIFY(response.allFailed());
}

void UpdaterTest::readyToInstall() {
    const QDateTime noon(QDate(2026, 10, 22), QTime(12, 0));
    const QDateTime night(QDate(2026, 10, 22), QTime(23, 0));
    const Settings automatic;
    const auto settings = [](const bool isAutomatic, const int minutes, const QString &start = {},
                             const QString &end = {}) {
        return Settings{.automatic = isAutomatic,
                        .offline = false,
                        .inactiveMinutes = minutes,
                        .windowStart = start,
                        .windowEnd = end};
    };
    constexpr std::chrono::seconds idle = 45min;

    struct Case {
        const char *name = nullptr;
        Settings settings;
        bool tonight = false;
        bool inUse = false;
        std::chrono::seconds inactive{};
        bool installed = false;
        QDateTime now;
        bool want = false;
    };
    const QList<Case> cases{
        {"idle long enough", automatic, false, false, idle, false, noon, true},
        {"in use", automatic, false, true, 0s, false, noon, false},
        {"in use with no wait", settings(true, 0), false, true, 0s, false, noon, false},
        {"no wait", settings(true, 0), false, false, 0s, false, noon, true},
        {"used recently", automatic, false, false, 2min, false, noon, false},
        {"already installed", automatic, false, false, idle, true, noon, false},
        {"automatic off", settings(false, 30), false, false, idle, false, noon, false},
        {"outside window", settings(true, 30, u"01:00"_s, u"05:00"_s), false, false, idle, false, noon, false},
        {"tonight ignores automatic and recent installs", settings(false, 30), true, false, idle, true, night, true},
        {"tonight waits for the night", automatic, true, false, idle, false, noon, false},
        {"tonight uses the window", settings(false, 30, u"11:00"_s, u"13:00"_s), true, false, idle, false, noon, true},
    };
    for (const Case &c: cases) {
        QString reason;
        QVERIFY2(UltramarineUpdates::readyToInstall(c.settings, c.tonight, c.inUse, c.inactive, c.installed, c.now,
                                                    &reason) == c.want,
                 c.name);
    }
}

void UpdaterTest::sessionInUse() {
    const QList<LoginSession> sessions = parseShowSessions(u"Id=2\nName=cypress\nClass=user\nType=wayland\nActive=yes\n"
                                                           u"IdleHint=no\nLockedHint=yes\n\n"
                                                           u"Id=3\nName=cypress\nClass=manager\nType=unspecified\n"
                                                           u"Active=yes\nIdleHint=no\nLockedHint=no\n\n"
                                                           u"Id=5\nName=guest\nClass=user\nType=x11\nActive=no\n"
                                                           u"IdleHint=no\nLockedHint=no\n\n"
                                                           u"Id=7\nName=admin\nClass=user\nType=tty\nActive=yes\n"
                                                           u"IdleHint=no\nLockedHint=no\n"_s);
    QCOMPARE(sessions.size(), 4);
    QVERIFY(!UltramarineUpdates::sessionInUse(sessions, u"cypress"_s));
    const std::optional<LoginSession> admin = UltramarineUpdates::sessionInUse(sessions);
    QVERIFY(admin);
    QCOMPARE(admin->id, u"7"_s);

    LoginSession idle;
    idle.sessionClass = u"user"_s;
    idle.type = u"wayland"_s;
    idle.active = true;
    idle.idle = true;
    QVERIFY(!idle.inUse());
}

void UpdaterTest::timestamps() {
    const QTemporaryDir dir;
    const QString stamp = dir.path() + "/x/stamp"_L1;
    const QDateTime now = referenceTime();
    QVERIFY(!within(stamp, 1h, now));
    QVERIFY(writeTimestamp(stamp, now.addSecs(-30 * 60LL), nullptr));
    QVERIFY(within(stamp, 1h, now));
    QVERIFY(!within(stamp, 10min, now));
    QVERIFY(writeTimestamp(stamp, now.addSecs(60 * 60LL), nullptr));
    QVERIFY(!within(stamp, 24h, now));
    QVERIFY(writeFile(stamp, "9223372036854775807\n"));
    QVERIFY(!readTimestamp(stamp));
    QVERIFY(!within(stamp, 24h, now));
    QVERIFY(!parseTimestamp("-9223372036854775807"));
    QCOMPARE(parseTimestamp(" 1800000000\n"), now);
}

void UpdaterTest::activityTracker() {
    const QTemporaryDir dir;
    const ActivityTracker tracker = trackerIn(dir);
    QCOMPARE(tracker.inactiveFor(false, referenceTime()), 0s);
    for (int minutes = 5; minutes <= 25; minutes += 5) {
        QCOMPARE(tracker.inactiveFor(false, minutesLater(minutes)),
                 std::chrono::seconds(std::chrono::minutes(minutes)));
    }
    QCOMPARE(tracker.inactiveFor(true, minutesLater(30)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(35)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(40)), std::chrono::seconds(5min));
}

void UpdaterTest::activityTrackerRestartsAfterGaps() {
    const QTemporaryDir dir;
    const ActivityTracker tracker = trackerIn(dir);
    QCOMPARE(tracker.inactiveFor(false, referenceTime()), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(5)), std::chrono::seconds(5min));
    QCOMPARE(tracker.inactiveFor(false, minutesLater(15)), std::chrono::seconds(15min));
    QCOMPARE(tracker.inactiveFor(false, minutesLater(15 + 8 * 60)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(20 + 8 * 60)), std::chrono::seconds(5min));
    QCOMPARE(tracker.inactiveFor(false, minutesLater(10)), 0s);

    const QTemporaryDir upgraded;
    QVERIFY(writeTimestamp(upgraded.path() + "/inactive-since"_L1, referenceTime(), nullptr));
    const ActivityTracker fresh = trackerIn(upgraded);
    QCOMPARE(fresh.inactiveFor(false, minutesLater(60)), 0s);
    QCOMPARE(fresh.inactiveFor(false, minutesLater(65)), std::chrono::seconds(5min));
}

void UpdaterTest::activityTrackerCountsFromIdleHint() {
    const QTemporaryDir dir;
    const ActivityTracker tracker = trackerIn(dir);
    QCOMPARE(tracker.inactiveFor(true, referenceTime()), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(5), minutesLater(2)), std::chrono::seconds(3min));
    QCOMPARE(tracker.inactiveFor(false, minutesLater(10), minutesLater(9)), std::chrono::seconds(8min));

    QCOMPARE(tracker.inactiveFor(true, minutesLater(15)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(20), minutesLater(1)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(25)), std::chrono::seconds(5min));
    QCOMPARE(tracker.inactiveFor(true, minutesLater(30)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(35), minutesLater(40)), 0s);
    QCOMPARE(tracker.inactiveFor(true, minutesLater(40)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(120), minutesLater(100)), 0s);
}

void UpdaterTest::sessionsIdleSince() {
    const QList<LoginSession> sessions =
        parseShowSessions(u"Id=2\nName=cypress\nUser=1000\nClass=user\nType=wayland\n"
                          u"Active=yes\nIdleHint=yes\nIdleSinceHint=1800000000000000\n"
                          u"LockedHint=no\n\n"
                          u"Id=3\nName=cypress\nUser=1000\nClass=manager\n"
                          u"Type=unspecified\nActive=yes\nIdleHint=no\nIdleSinceHint=0\n\n"
                          u"Id=7\nName=admin\nUser=1001\nClass=user\nType=tty\n"
                          u"Active=yes\nIdleHint=yes\nIdleSinceHint=1800000600000000\n"_s);
    QCOMPARE(sessions.size(), 3);
    QCOMPARE(sessions.at(0).uid, 1000U);
    QCOMPARE(sessions.at(0).idleSince, referenceTime());
    QVERIFY(!sessions.at(1).idleSince);
    QCOMPARE(UltramarineUpdates::sessionsIdleSince(sessions), minutesLater(10));
    QCOMPARE(UltramarineUpdates::sessionsIdleSince(sessions, u"cypress"_s), referenceTime());
    QVERIFY(!UltramarineUpdates::sessionsIdleSince(sessions, u"guest"_s));

    QList<LoginSession> locked = sessions;
    locked[2].idle = false;
    locked[2].locked = true;
    QVERIFY(!UltramarineUpdates::sessionsIdleSince(locked));
    QCOMPARE(UltramarineUpdates::sessionsIdleSince(locked, u"cypress"_s), referenceTime());
}

void UpdaterTest::idleInhibited() {
    const QList<LoginSession> sessions{
        {.id = u"2"_s,
         .name = u"cypress"_s,
         .sessionClass = u"user"_s,
         .type = u"wayland"_s,
         .active = true,
         .uid = 1000,
         .locked = false,
         .idle = false,
         .idleSince = {}},
        {.id = u"c1"_s,
         .name = u"sddm"_s,
         .sessionClass = u"greeter"_s,
         .type = u"wayland"_s,
         .active = false,
         .uid = 976,
         .locked = false,
         .idle = false,
         .idleSince = {}},
    };
    const auto inhibitor = [](const QString &what, const QString &who, const uint uid,
                              const QString &mode = u"block"_s) {
        return Inhibitor{.what = what, .who = who, .why = u"why"_s, .mode = mode, .uid = uid, .pid = 42};
    };
    QVERIFY(!UltramarineUpdates::idleInhibited({}, sessions));
    QVERIFY(UltramarineUpdates::idleInhibited({inhibitor(u"sleep:idle"_s, u"mpv"_s, 1000)}, sessions));
    QVERIFY(UltramarineUpdates::idleInhibited({inhibitor(u"idle"_s, u"mpv"_s, 1000)}, sessions, u"cypress"_s));
    QVERIFY(!UltramarineUpdates::idleInhibited({inhibitor(u"idle"_s, u"mpv"_s, 1000)}, sessions, u"admin"_s));
    const QList<Inhibitor> ignored{
        inhibitor(u"sleep"_s, u"mpv"_s, 1000),
        inhibitor(u"idle"_s, u"mpv"_s, 1000, u"delay"_s),
        inhibitor(u"idle"_s, u"daemon"_s, 0),
        inhibitor(u"handle-power-key:handle-lid-switch:idle"_s, u"PowerDevil"_s, 1000),
        inhibitor(u"idle"_s, QString::fromLatin1(inhibitorName), 1000),
        inhibitor(u"idle"_s, u"greeter"_s, 976),
        inhibitor(u"idle"_s, u"mpv"_s, 1234),
    };
    QVERIFY(!UltramarineUpdates::idleInhibited(ignored, sessions));

    // PowerDevil reports everything as inhibited in background sessions
    QVERIFY(hasActiveSession(sessions, u"cypress"_s));
    QVERIFY(!hasActiveSession(sessions, u"sddm"_s));
    QVERIFY(!hasActiveSession(sessions, u"guest"_s));
}

void UpdaterTest::screenInhibitedMarker() {
    const QTemporaryDir dir;
    const QString path = dir.path() + "/screen-inhibited"_L1;
    const QDateTime now = referenceTime();
    const uint uid = getuid();
    QVERIFY(!freshScreenInhibitedMarker(path, uid, maxPollGap, now));
    QVERIFY(writeTimestamp(path, now.addSecs(-60), nullptr));
    QVERIFY(freshScreenInhibitedMarker(path, uid, maxPollGap, now));
    QVERIFY(!freshScreenInhibitedMarker(path, uid, maxPollGap, now.addSecs(3600)));
    QVERIFY(!freshScreenInhibitedMarker(path, uid, maxPollGap, now.addSecs(-120)));
    QVERIFY(!freshScreenInhibitedMarker(path, uid + 1, maxPollGap, now));
    const QString link = dir.path() + "/link"_L1;
    QVERIFY(QFile::link(path, link));
    QVERIFY(!freshScreenInhibitedMarker(link, uid, maxPollGap, now));
    const QString linkedDir = dir.path() + "/linked"_L1;
    QVERIFY(QFile::link(dir.path(), linkedDir));
    QVERIFY(!freshScreenInhibitedMarker(linkedDir + "/screen-inhibited"_L1, uid, maxPollGap, now));
    const QString fifo = dir.path() + "/fifo"_L1;
    QCOMPARE(mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    QVERIFY(!freshScreenInhibitedMarker(fifo, uid, maxPollGap, now));
    QVERIFY(writeFile(path, "9223372036854775807\n"));
    QVERIFY(!freshScreenInhibitedMarker(path, uid, maxPollGap, now));
}

void UpdaterTest::installLock() {
    const QTemporaryDir dir;
    const QString path = dir.path() + "/updates.lock"_L1;
    {
        InstallLock first(path);
        QCOMPARE(first.tryLock(), InstallLock::Result::Acquired);
        InstallLock second(path);
        QCOMPARE(second.tryLock(), InstallLock::Result::Busy);
    }
    InstallLock again(path);
    QCOMPARE(again.tryLock(), InstallLock::Result::Acquired);

    QString error;
    InstallLock missing(dir.path() + "/missing/updates.lock"_L1);
    QCOMPARE(missing.tryLock(&error), InstallLock::Result::Failed);
    QVERIFY(!error.isEmpty());
}

void UpdaterTest::installLockWaits() {
    const QTemporaryDir dir;
    const QString path = dir.path() + "/updates.lock"_L1;
    InstallLock holder(path);
    QCOMPARE(holder.tryLock(), InstallLock::Result::Acquired);
    InstallLock waiting(path);
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(waiting.tryLock(nullptr, 300ms), InstallLock::Result::Busy);
    QVERIFY(timer.elapsed() >= 300);
}

namespace {
    bool parseCommandLine(QCommandLineParser &parser, const QStringList &arguments) {
        setUpParser(parser);
        return parser.parse(QStringList{u"ultramarine-updates"_s} + arguments);
    }

    QStringList escalated(const QStringList &arguments) {
        QCommandLineParser parser;
        if (!parseCommandLine(parser, arguments)) {
            return {u"parse error: "_s + parser.errorText()};
        }
        return escalatedCommandLine(parser.positionalArguments().value(0), parser);
    }

    QString optionError(const QStringList &arguments) {
        QCommandLineParser parser;
        if (!parseCommandLine(parser, arguments)) {
            return u"parse error"_s;
        }
        return checkOptions(parser.positionalArguments().value(0), parser);
    }
} // namespace

void UpdaterTest::escalatedCommandLine() {
    // polkit rules match argv[1], so the command must come first
    QCOMPARE(escalated({u"--yes"_s, u"apply"_s}), (QStringList{u"apply"_s, u"--yes"_s}));
    QCOMPARE(escalated({u"apply"_s, u"-y"_s}), (QStringList{u"apply"_s, u"--yes"_s}));
    QCOMPARE(escalated({u"settings"_s, u"--automatic"_s, u"false"_s, u"--window=01:00-05:00"_s}),
             (QStringList{u"settings"_s, u"--automatic=false"_s, u"--window=01:00-05:00"_s}));
    QCOMPARE(escalated({u"settings"_s, u"--window="_s}), (QStringList{u"settings"_s, u"--window="_s}));
    QCOMPARE(escalated({u"settings"_s, u"--inactive-minutes"_s, u"5 --offline=true"_s}),
             (QStringList{u"settings"_s, u"--inactive-minutes=5 --offline=true"_s}));
    QCOMPARE(escalated({u"schedule"_s, u"--cancel"_s}), (QStringList{u"schedule"_s, u"--cancel"_s}));
}

void UpdaterTest::checksOptions() {
    QVERIFY(optionError({u"apply"_s, u"--yes"_s}).isEmpty());
    QVERIFY(optionError({u"apply"_s, u"-y"_s}).isEmpty());
    QVERIFY(optionError({u"list"_s, u"--json"_s}).isEmpty());
    QVERIFY(optionError({u"settings"_s, u"--json"_s, u"--offline=true"_s}).isEmpty());
    QVERIFY(optionError({u"schedule"_s, u"--status"_s, u"--json"_s}).isEmpty());
    QVERIFY(optionError({u"run-user"_s, u"--tonight"_s}).isEmpty());
    QVERIFY(!optionError({u"apply"_s, u"--window=01:00-02:00"_s}).isEmpty());
    QVERIFY(!optionError({u"apply"_s, u"--tonight"_s}).isEmpty());
    QVERIFY(!optionError({u"schedule"_s, u"--automatic=false"_s}).isEmpty());
    QVERIFY(!optionError({u"settings"_s, u"--yes"_s}).isEmpty());
    QVERIFY(!optionError({u"upgrade"_s}).isEmpty());
    QVERIFY(optionError({u"schedule"_s}).isEmpty());
    QVERIFY(optionError({u"schedule"_s, u"--cancel"_s}).isEmpty());
    QVERIFY(optionError({u"schedule"_s, u"--status"_s}).isEmpty());
    QCOMPARE(optionError({u"schedule"_s, u"--cancel"_s, u"--status"_s}), u"--cancel can't be used with --status."_s);
    QCOMPARE(optionError({u"schedule"_s, u"--json"_s}), u"--json can't be used with schedule."_s);
    QCOMPARE(optionError({u"schedule"_s, u"--cancel"_s, u"--json"_s}), u"--json can't be used with --cancel."_s);
    QVERIFY(!optionError({}).isEmpty());
    QCOMPARE(optionError({u"apply"_s, u"--bogus"_s}), u"parse error"_s);
}

void UpdaterTest::applyResultCountsAsInstalled() {
    ApplyResult nothing;
    QVERIFY(nothing.ok());
    QVERIFY(nothing.countsAsInstalled());

    ApplyResult partial;
    partial.succeed();
    partial.fail(u"Updating system Flatpaks failed (exit code 1)."_s);
    QVERIFY(!partial.ok());
    QVERIFY(partial.countsAsInstalled());

    ApplyResult primary;
    primary.succeed();
    primary.fail(u"Updating RPM packages failed (exit code 1)."_s, true);
    QVERIFY(!primary.countsAsInstalled());

    ApplyResult allFailed;
    allFailed.fail(u"a"_s);
    allFailed.fail(u"b"_s);
    QVERIFY(!allFailed.countsAsInstalled());
    QCOMPARE(allFailed.error(), u"a\nb"_s);
}

namespace {
    struct TimerFixture {
        QTemporaryDir dir;
        QString lock = dir.path() + "/updates.lock"_L1;
        QString installed = dir.path() + "/last-automatic-update"_L1;
        QString failed = dir.path() + "/last-failed-update"_L1;
        int runs = 0;

        bool run(const QDateTime &now, const ApplyResult &result, bool *installedNow, QString *error) {
            return installFromTimer(
                lock, dir.path(), installed, now,
                [this, &result] {
                    ++runs;
                    return result;
                },
                installedNow, error);
        }
    };

    ApplyResult succeeded() {
        ApplyResult result;
        result.updated = true;
        result.succeed();
        return result;
    }
} // namespace

void UpdaterTest::installFromTimerRecordsInstalls() {
    TimerFixture fixture;
    const QDateTime now = referenceTime();
    bool installedNow = false;
    QString error;
    QVERIFY(fixture.run(now, succeeded(), &installedNow, &error));
    QVERIFY(installedNow);
    QVERIFY(error.isEmpty());
    QCOMPARE(readTimestamp(fixture.installed), now);
    QVERIFY(!QFile::exists(fixture.failed));
}

void UpdaterTest::installFromTimerRetriesAfterAnHour() {
    TimerFixture fixture;
    const QDateTime now = referenceTime();
    ApplyResult failure;
    failure.updated = true;
    failure.fail(u"Updating RPM packages failed (exit code 1)."_s, true);
    bool installedNow = true;
    QString error;
    QVERIFY(!fixture.run(now, failure, &installedNow, &error));
    QVERIFY(!installedNow);
    QCOMPARE(error, u"Updating RPM packages failed (exit code 1)."_s);
    QVERIFY(!QFile::exists(fixture.installed));
    QCOMPARE(readTimestamp(fixture.failed), now);

    error.clear();
    QVERIFY(fixture.run(now.addSecs(5LL * 60), succeeded(), &installedNow, &error));
    QVERIFY(!installedNow);
    QCOMPARE(fixture.runs, 1);

    QVERIFY(fixture.run(now.addSecs(61LL * 60), succeeded(), &installedNow, &error));
    QVERIFY(installedNow);
    QCOMPARE(fixture.runs, 2);
    QVERIFY(!QFile::exists(fixture.failed));
}

void UpdaterTest::installFromTimerKeepsPartialInstalls() {
    TimerFixture fixture;
    const QDateTime now = referenceTime();
    ApplyResult partial = succeeded();
    partial.fail(u"Updating system Flatpaks failed (exit code 1)."_s);
    bool installedNow = false;
    QString error;
    QVERIFY(!fixture.run(now, partial, &installedNow, &error));
    QVERIFY(installedNow);
    QCOMPARE(error, u"Updating system Flatpaks failed (exit code 1)."_s);
    QCOMPARE(readTimestamp(fixture.installed), now);
    QVERIFY(!QFile::exists(fixture.failed));
    QVERIFY(within(fixture.installed, 20h, now.addSecs(2LL * 60 * 60)));
}

void UpdaterTest::installFromTimerSkipsWhileBusy() {
    TimerFixture fixture;
    InstallLock holder(fixture.lock);
    QCOMPARE(holder.tryLock(), InstallLock::Result::Acquired);
    bool installedNow = true;
    QString error;
    QVERIFY(fixture.run(referenceTime(), succeeded(), &installedNow, &error));
    QVERIFY(!installedNow);
    QVERIFY(error.isEmpty());
    QCOMPARE(fixture.runs, 0);

    QVERIFY(!installFromTimer(fixture.dir.path() + "/missing/updates.lock"_L1, fixture.dir.path(), fixture.installed,
                              referenceTime(), succeeded, &installedNow, &error));
    QVERIFY(!error.isEmpty());
}

void UpdaterTest::notifiesOnlyAfterASuccessfulListing() {
    const QTemporaryDir dir;
    const QString checked = dir.path() + "/last-update-check"_L1;
    const QDateTime now = referenceTime();
    int listings = 0;
    QList<qsizetype> notified;
    ListResponse response;
    const auto list = [&listings, &response] {
        ++listings;
        return response;
    };
    const auto notify = [&notified](const qsizetype count) { notified.append(count); };

    response.queriedSources = 2;
    response.errors = {u"rpm: no network"_s, u"flatpak system: no network"_s};
    notifyPendingUpdates(checked, now, list, notify);
    QVERIFY(notified.isEmpty());
    QVERIFY(!QFile::exists(checked));
    notifyPendingUpdates(checked, now.addSecs(5LL * 60), list, notify);
    QCOMPARE(listings, 2);

    response.errors = {u"flatpak system: no network"_s};
    PackageUpdate update;
    update.id = u"tree.x86_64"_s;
    response.updates = {update};
    notifyPendingUpdates(checked, now.addSecs(10LL * 60), list, notify);
    QCOMPARE(notified, (QList<qsizetype>{1}));
    QCOMPARE(readTimestamp(checked), now.addSecs(10LL * 60));

    notifyPendingUpdates(checked, now.addSecs(60LL * 60), list, notify);
    QCOMPARE(listings, 3);
    notifyPendingUpdates(checked, now.addSecs(10LL * 60 + 24LL * 60 * 60), list, notify);
    QCOMPARE(listings, 4);
    QCOMPARE(notified.size(), 2);

    response.updates.clear();
    notifyPendingUpdates(checked, now.addSecs(3LL * 24 * 60 * 60), list, notify);
    QCOMPARE(notified.size(), 2);
    QCOMPARE(readTimestamp(checked), now.addSecs(3LL * 24 * 60 * 60));
}

void UpdaterTest::parsesBootcStatusCache() {
    const QByteArray status = R"({"apiVersion":"org.containers.bootc/v1","kind":"BootcHost","status":{"staged":null}})";
    const QByteArray cache = bootcStatusCache(status, u"boot-a"_s);
    const std::optional<QByteArray> parsed = parseBootcStatusCache(cache, u"boot-a"_s);
    QVERIFY(parsed);
    QCOMPARE(QJsonDocument::fromJson(*parsed), QJsonDocument::fromJson(status));
    QVERIFY(!parseBootcStatusCache(cache, u"boot-b"_s));
    QVERIFY(!parseBootcStatusCache(cache, {}));
    QVERIFY(!parseBootcStatusCache("nope", u"boot-a"_s));
}

namespace {
    PackageKitPaths fakePackageKitPaths(const QTemporaryDir &dir) {
        const QString lib = dir.path() + "/var/lib/PackageKit"_L1;
        QDir().mkpath(lib);
        return {.preparedUpdate = lib + "/prepared-update"_L1,
                .preparedUpgrade = lib + "/prepared-upgrade"_L1,
                .trigger = dir.path() + "/system-update"_L1,
                .results = lib + "/offline-update-competed"_L1,
                .osRelease = dir.path() + "/os-release"_L1,
                .rpmDatabase = dir.path() + "/rpmdb.sqlite"_L1};
    }

    // as pk-offline-update writes them after a failed update in PackageKit 1.4
    const QByteArray failedResults = R"([PackageKit Offline Update Results]
Success=false
ErrorCode=failed-initialization
ErrorDetails=Transaction failed: rpm transaction failed:\n  - file /usr/bin/foo conflicts
Role=update-packages
)";
} // namespace

void UpdaterTest::classifiesSystemUpdateTarget() {
    QCOMPARE(classifySystemUpdateTarget({}), SystemUpdateTrigger::None);
    QCOMPARE(classifySystemUpdateTarget(u"/var/lib/PackageKit/prepared-update"_s),
             SystemUpdateTrigger::PackageKitUpdate);
    QCOMPARE(classifySystemUpdateTarget(u"/var/lib/PackageKit/prepared-upgrade"_s),
             SystemUpdateTrigger::PackageKitUpgrade);
    QCOMPARE(classifySystemUpdateTarget(u"/usr/lib/sysimage/libdnf5/offline"_s), SystemUpdateTrigger::Other);
    QCOMPARE(classifySystemUpdateTarget(u"var/lib/PackageKit/prepared-update"_s), SystemUpdateTrigger::Other);
    QCOMPARE(classifySystemUpdateTarget(u"/var/lib/PackageKit"_s), SystemUpdateTrigger::Other);
}

void UpdaterTest::detectsPendingOfflineUpdate() {
    const QTemporaryDir dir;
    const PackageKitPaths paths = fakePackageKitPaths(dir);
    QCOMPARE(systemUpdateTrigger(paths), SystemUpdateTrigger::None);
    QVERIFY(!offlineUpdatePending(paths));

    QVERIFY(QFile::link(paths.preparedUpdate, paths.trigger));
    QCOMPARE(systemUpdateTrigger(paths), SystemUpdateTrigger::PackageKitUpdate);
    QVERIFY(!offlineUpdatePending(paths));

    QVERIFY(writeFile(paths.preparedUpdate, "[update]\nprepared_ids=tree;2.2.1-1.fc45;x86_64;fedora,\n"));
    QVERIFY(offlineUpdatePending(paths));

    QVERIFY(QFile::remove(paths.trigger));
    QVERIFY(!offlineUpdatePending(paths));

    QVERIFY(QFile::link(paths.preparedUpgrade, paths.trigger));
    QCOMPARE(systemUpdateTrigger(paths), SystemUpdateTrigger::PackageKitUpgrade);
    QVERIFY(!offlineUpdatePending(paths));
    QVERIFY(QFile::remove(paths.trigger));

    QVERIFY(writeFile(paths.trigger, {}));
    QCOMPARE(systemUpdateTrigger(paths), SystemUpdateTrigger::Other);
    QVERIFY(!offlineUpdatePending(paths));
}

void UpdaterTest::parsesOfflineResults() {
    QCOMPARE(parseOfflineResultsError(failedResults),
             u"Transaction failed: rpm transaction failed:\n  - file /usr/bin/foo conflicts"_s);
    QCOMPARE(parseOfflineResultsError("[PackageKit Offline Update Results]\nSuccess=true\nRole=update-packages\n"
                                      "Packages=tree;2.2.1-1.fc45;x86_64;updates,\n"),
             QString());
    QCOMPARE(
        parseOfflineResultsError("[PackageKit Offline Update Results]\nSuccess=false\nErrorDetails=\\sa\\\\b\\tc\n"),
        u"a\\b\tc"_s);
    QCOMPARE(parseOfflineResultsError("[PackageKit Offline Update Results]\nSuccess=false\nErrorCode=no-network\n"),
             u"no-network"_s);
    QCOMPARE(parseOfflineResultsError("[PackageKit Offline Update Results]\nSuccess=false\n"), u"unknown error"_s);
    QCOMPARE(parseOfflineResultsError("[Something Else]\nSuccess=false\nErrorDetails=nope\n"), QString());
    QCOMPARE(parseOfflineResultsError({}), QString());
    QCOMPARE(parseOfflineResultsError("garbage"), QString());
}

void UpdaterTest::reportsLastOfflineUpdateError() {
    const QTemporaryDir dir;
    const PackageKitPaths paths = fakePackageKitPaths(dir);
    QCOMPARE(lastOfflineUpdateError(paths), QString());

    QVERIFY(writeFile(paths.results, failedResults));
    QVERIFY(lastOfflineUpdateError(paths).startsWith("Transaction failed"_L1));

    QVERIFY(writeFile(paths.rpmDatabase, "rpmdb"));
    const QDateTime failed = QDateTime::currentDateTime().addSecs(-3600);
    QFile results(paths.results);
    QVERIFY(results.open(QIODevice::ReadWrite));
    QVERIFY(results.setFileTime(failed, QFileDevice::FileModificationTime));
    results.close();
    QCOMPARE(lastOfflineUpdateError(paths), QString());

    QFile rpmDatabase(paths.rpmDatabase);
    QVERIFY(rpmDatabase.open(QIODevice::ReadWrite));
    QVERIFY(rpmDatabase.setFileTime(failed.addSecs(-60), QFileDevice::FileModificationTime));
    rpmDatabase.close();
    QVERIFY(!lastOfflineUpdateError(paths).isEmpty());
}

void UpdaterTest::parsesOsReleaseVersionId() {
    QCOMPARE(parseOsReleaseVersionId("NAME=\"Ultramarine Linux\"\nVERSION=\"45\"\nID=ultramarine\n"
                                     "ID_LIKE=fedora\nVERSION_ID=45\nPLATFORM_ID=\"platform:f45\"\n"),
             u"45"_s);
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID=\"45\"\n"), u"45"_s);
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID='24.04'\n"), u"24.04"_s);
    QCOMPARE(parseOsReleaseVersionId("  VERSION_ID = 45 \n"), u"45"_s);
    QCOMPARE(parseOsReleaseVersionId("ID=fedora\n"), QString());
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID=\"\"\n"), QString());
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID=../../etc\n"), QString());
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID=\"45/x\"\n"), QString());
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID=-45\n"), QString());
    QCOMPARE(parseOsReleaseVersionId("VERSION_ID=.hidden\n"), QString());

    const QTemporaryDir dir;
    const PackageKitPaths paths = fakePackageKitPaths(dir);
    QVERIFY(writeFile(paths.osRelease, "ID=fedora\nVERSION_ID=45\n"));
    QCOMPARE(packageKitCacheDir(paths), u"/var/cache/PackageKit/45/metadata"_s);
    QVERIFY(writeFile(paths.osRelease, "ID=fedora\n"));
    QCOMPARE(packageKitCacheDir(paths), QString());
}

void UpdaterTest::parsesPkgcliError() {
    QCOMPARE(pkgcliError("Error: Failed to get updates: Cannot download repomd.xml\n"),
             u"Failed to get updates: Cannot download repomd.xml"_s);
    QCOMPARE(
        pkgcliError("Downloading packages\nError: Failed to trigger offline update: Prepared update not found\n\n"),
        u"Failed to trigger offline update: Prepared update not found"_s);
    QCOMPARE(pkgcliError("Fehler: kaputt\n"), u"Fehler: kaputt"_s);
    QCOMPARE(pkgcliError({}), QString());
}

QTEST_GUILESS_MAIN(UpdaterTest)

#include "test_updater.moc"
