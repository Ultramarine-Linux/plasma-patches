# Discover patches

On Ultramarine, um handles updates (`um upgrade`), and System Settings shows them
through `kcm_ultramarine_updates`. These patches take the update functionality out
of Discover so the two don't compete.

**Target: UM45** (Fedora 45 / `umrawhide`). Nothing here applies to UM44.

| file | Discover version |
| --- | --- |
| `0001-Remove-Discover-updates-UI-in-favor-of-um.patch` | 6.8.x (Fedora rawhide is on 6.8.1) |
| `6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch` | 6.7.5 (current f44/f45), for testing only |
| `plasma-discover.spec` | draft replacement for `ultramarine/plasma-discover/plasma-discover.spec` on `umrawhide` |

## What the patch does

- Removes the Updates page as a Discover mode. `discover --mode update` no longer
  works, and the page is gone from `--listmodes`.
- Replaces the "Updates" entry in the sidebar and the mobile tab bar with a link that
  opens System Settings > Updates.
- Changes "Configure Updates…" (Settings page and feedback menu) to open
  `kcm_ultramarine_updates` instead of Discover's `kcm_updates`.
- Sends `ResourcesModel::switchToUpdates` (used by the PackageKit distro-upgrade
  banner) to System Settings > Updates.
- Makes `discover --headless-update` exit with an error instead of applying updates.

The upstream autotests that open the Updates mode (`toplevels.qml`,
`updateandinstall.qml`) no longer apply. They aren't run during packaging.

## Why the spec has to change

Today `ultramarine/plasma-discover` only builds `plasma-discover-packagekit`. It deletes
everything else from the buildroot, so the Discover app, `-libs`, `-flatpak`,
`-notifier` and `kcm_updates` all come from Fedora. The patch changes the app
(`plasma-discover` binary and its QML), so Ultramarine has to ship the app.

Fedora pins every Discover subpackage to the exact `plasma-discover = %{version}-%{release}`.
Once Ultramarine ships its own `plasma-discover`, Fedora's `-libs`, `-flatpak`, `-kns`
and `-notifier` can't be installed with it. So the draft spec:

- builds main, `-libs`, `-packagekit`, `-flatpak` and `-kns` from one source
- uses `-DWITH_KCM=OFF` (no Discover "Software Update" KCM) and `-DWITH_NOTIFIER=OFF`
  (no update notifier daemon, autostart entry or unattended/offline updates), and
  removes the leftover backend notifier plugins
  - update notifications and automatic updates come from umcli's
    `um-upgrade-user.timer` and `um-upgrade-automatic.timer` instead
- obsoletes Fedora's `-notifier`, `-offline-updates`, `-rpm-ostree` and `-snap`
- drops `discover-pk-refresh-timer.patch` (it only affected the notifier) and the
  revert for KDE bug 512957 (fixed upstream and already in 6.8.1). The unused
  `discoverrc` (`UseOfflineUpdates=true`) can go too.

The draft was built with `rpmbuild -ba` on Fedora rawhide (6.8.1). All five packages
built with no unpackaged or missing files, installed against Fedora's repos, and
rpmlint found nothing meaningful.

`.um45` sorts above `.fc45`, so Ultramarine's build wins at the same version.
`update.rhai` already bumps this package with each Plasma release. Every bump now
also needs the patch to apply, but it's small and mostly QML.

### Cheaper alternative (no source patch)

If maintaining the patch isn't worth it, most of the effect needs no source changes:

- Keep shipping only `-packagekit`, but drop `DiscoverPackageKitNotifier.so`, and
  `Obsoletes: plasma-discover-notifier` from some Ultramarine package (nothing depends
  on it). This stops update notifications and unattended/offline updates.
- Hide Discover's KCM through `kde-settings`: System Settings honors
  `[KDE Control Module Restrictions]` in `/etc/xdg/kdeglobals`, e.g. `kcm_updates=false`.

Discover's Updates tab would still exist and still work, though.
