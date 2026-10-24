// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

// Tests for the update helper's parsers and decisions (updater/).

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "activity.h"
#include "apps.h"
#include "flatpak.h"
#include "image.h"
#include "nix.h"
#include "rpm.h"
#include "schedule.h"
#include "settings.h"

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
        static void resolvesNixApps();
        static void parsesDeterminateNixVersions();
        static void parsesImageStatus();
        static void settingsRoundTrip();
        static void inWindow();
        static void validatesSettings();
        static void parsesTimerShow();
        static void readyToInstall();
        static void sessionInUse();
        static void timestamps();
        static void activityTracker();
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

    // Legacy: type="desktop", no <launchable>, ID already ends in .desktop.
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

    // Name and ID from metainfo, icon from the launchable desktop file.
    const AppFiles dolphin{.metainfo = {u"/usr/share/metainfo/org.kde.dolphin.plugin.xml"_s,
                                        u"/usr/share/metainfo/org.kde.dolphin.appdata.xml"_s},
                           .desktop = {},
                           .applicationsDir = {}};
    const std::optional<AppInfo> app = resolveApp(u"dolphin"_s, &dolphin, read);
    QVERIFY(app);
    QCOMPARE(*app,
             (AppInfo{.id = u"org.kde.dolphin.desktop"_s, .name = u"Dolphin"_s, .icon = u"system-file-manager"_s}));

    // Metainfo that isn't a desktop application wins over a stray launcher.
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
    // Store-path installs and inactive entries can't be upgraded.
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

    // Same store paths: nothing to upgrade.
    QVERIFY(!UltramarineUpdates::nixUpdateFor(
        element, {.error = false, .name = {}, .version = {}, .outPaths = {u"/nix/store/a-flox-1.17.0-g486737b"_s}}));
    // A rebuild without a version bump still counts; the version falls back to the name.
    update = UltramarineUpdates::nixUpdateFor(element,
                                              {.error = false,
                                               .name = u"flox-1.17.0-g486737b"_s,
                                               .version = {},
                                               .outPaths = {u"/nix/store/c-flox-1.17.0-g486737b"_s}});
    QVERIFY(update);
    QCOMPARE(update->newVersion, u"1.17.0-g486737b"_s);
    QVERIFY(!UltramarineUpdates::nixUpdateFor(element, {.error = true, .name = {}, .version = {}, .outPaths = {}}));
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
    const std::optional<PackageUpdate> update = parseRpmOstreeStatus(status, u"Ultramarine Linux"_s, u"bootc"_s);
    QVERIFY(update);
    QCOMPARE(update->oldVersion, u"44.20261001.0"_s);
    QCOMPARE(update->newVersion, u"44.20261020.0"_s);
    QCOMPARE(update->backend, u"bootc"_s);
    QCOMPARE(update->source, u"Image (ostree-image-signed:docker://ghcr.io/ultramarine-linux/plasma-bootc:44)"_s);

    // No cached update, or it's what's booted: nothing to install.
    QVERIFY(!parseRpmOstreeStatus(R"({"deployments":[{"booted":true,"checksum":"abc"}],"cached-update":null})", {},
                                  u"bootc"_s));
    QVERIFY(!parseRpmOstreeStatus(
        R"({"deployments":[{"booted":true,"checksum":"abc"}],"cached-update":{"checksum":"abc"}})", {}, u"bootc"_s));
}

void UpdaterTest::settingsRoundTrip() {
    const QTemporaryDir dir;
    const QString path = dir.path() + "/updates/settings.json"_L1;
    qputenv("ULTRAMARINE_UPDATES_CONFIG", path.toLocal8Bit());

    Settings got;
    QString error;
    // A missing file gives the defaults.
    QVERIFY(loadSettings(&got, &error));
    QCOMPARE(got, Settings{});

    const Settings want{
        .automatic = false, .offline = true, .inactiveMinutes = 10, .windowStart = u"01:00"_s, .windowEnd = u"05:00"_s};
    QVERIFY2(saveSettings(want, &error), qPrintable(error));
    QVERIFY(loadSettings(&got, &error));
    QCOMPARE(got, want);
    QVERIFY(QFile::permissions(path) & QFile::ReadOther);

    // Missing keys keep their defaults.
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"offline": true})");
    file.close();
    QVERIFY(loadSettings(&got, &error));
    QCOMPARE(got,
             (Settings{.automatic = true, .offline = true, .inactiveMinutes = 30, .windowStart = {}, .windowEnd = {}}));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("nope");
    file.close();
    QVERIFY(!loadSettings(&got, &error));
    QCOMPARE(got, Settings{});

    QVERIFY(!parseSettings(R"({"automatic":"yes"})", &got, &error));
    QVERIFY(!parseSettings(R"({"inactive_minutes":1.5})", &got, &error));
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
        const char *name;
        Settings settings;
        bool tonight;
        bool inUse;
        std::chrono::seconds inactive;
        bool installed;
        QDateTime now;
        bool want;
    };
    const QList<Case> cases{
        {"idle long enough", automatic, false, false, idle, false, noon, true},
        {"in use", automatic, false, true, 0s, false, noon, false},
        {"in use with no wait", settings(true, 0), false, true, 0s, false, noon, false},
        {"no wait", settings(true, 0), false, false, 0s, false, noon, true},
        {"used recently", automatic, false, false, 10min, false, noon, false},
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
    // A locked graphical session, a user manager and an inactive session don't count.
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
    const QDateTime now = QDateTime::fromSecsSinceEpoch(1'800'000'000);
    QVERIFY(!within(stamp, 1h, now));
    QVERIFY(writeTimestamp(stamp, now.addSecs(-30 * 60LL), nullptr));
    QVERIFY(within(stamp, 1h, now));
    QVERIFY(!within(stamp, 10min, now));
    // A stamp from the future (clock changes) doesn't block forever.
    QVERIFY(writeTimestamp(stamp, now.addSecs(60 * 60LL), nullptr));
    QVERIFY(!within(stamp, 24h, now));
}

void UpdaterTest::activityTracker() {
    const QTemporaryDir dir;
    const ActivityTracker tracker(dir.path() + "/inactive-since"_L1);
    const QDateTime start = QDateTime::fromSecsSinceEpoch(1'800'000'000);
    const auto minutesLater = [&start](const int minutes) { return start.addSecs(minutes * 60LL); };
    QCOMPARE(tracker.inactiveFor(false, start), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(25)), std::chrono::seconds(25min));
    // Using the device resets the count.
    QCOMPARE(tracker.inactiveFor(true, minutesLater(30)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(35)), 0s);
    QCOMPARE(tracker.inactiveFor(false, minutesLater(40)), std::chrono::seconds(5min));
}

QTEST_GUILESS_MAIN(UpdaterTest)

#include "test_updater.moc"
