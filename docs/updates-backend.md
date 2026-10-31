# Update helper (`ultramarine-updates`)

The Updates KCM is a frontend for `ultramarine-updates`, a small C++/QtCore
helper in [`updater/`](../updater). It lists and installs updates from every
source on the system and runs automatic updates and Apply Tonight. It replaces
the `um upgrade` command proposed for umcli; umcli still backs the Tweaks KCM
(see [umcli backend](umcli-backend.md)).

## What it updates

| Source | Listing (as the user) | Installing |
| --- | --- | --- |
| RPM packages | `dnf check-update`, sizes from `dnf repoquery` and `rpm` | `dnf upgrade --refresh` as root, or downloaded with dnf and installed while restarting by PackageKit (see [Offline updates](#offline-updates)) |
| Image mode (bootc/rpm-ostree) | the update bootc/rpm-ostree already found (`bootc status`, `rpm-ostree status`) | `bootc upgrade`, `um env update` on `um env` systems, or `rpm-ostree upgrade`, as root |
| Flatpak | `flatpak remote-ls --updates`, per installation | `flatpak update --user` as the user, `--system` as root |
| Nix profile | `nix profile list` and a pure evaluation of what `nix profile upgrade` would install | `nix profile upgrade --all --impure`, as the user |
| Determinate Nix | `determinate-nixd version` | `determinate-nixd upgrade`, as root |
| Plasma widgets | KNewStuff (`plasmoids.knsrc`) compares the user's registry with the KDE Store | KNewStuff installs the new version, as the user |

On image mode systems (`/run/ostree-booted`), dnf is never used. The image
update is staged for the next boot either way, so the offline setting doesn't
apply there, and the KCM shows it as always on. Once an image is staged, it's no
longer listed as an update; `list` reports `restart_required` instead, and the
KCM offers to restart (through Plasma's logout screen). Download-only stages
(`bootc upgrade --download-only`) aren't applied when restarting, so they're
still listed.

Neither tool can check the registry without root, so the listing shows an image
update only after bootc or rpm-ostree found one. The system timer runs
`bootc upgrade --check` (or `rpm-ostree upgrade --check`) every 6 hours for
that, whether or not updates install automatically. `um env` systems track a
local image, so their base image updates show up only when installing.

`bootc status` also needs root on a booted system (it enters a mount namespace
first). So the system timer, `bootc upgrade --check` and installing keep a
world-readable copy of its output in
`/var/lib/ultramarine-updates/bootc-status.json`, tagged with the boot ID, which
the listing reads as the user. A copy from a previous boot is ignored, and
without one the listing falls back to `rpm-ostree status`.

Images update through this helper, which never restarts the device. Don't enable
bootc's `bootc-fetch-apply-updates.timer` alongside it: it runs
`bootc upgrade --apply`, which restarts as soon as an update is staged.

Plasma widgets are the ones installed from the KDE Store through "Get New
Widgets" (or Discover), which KNewStuff records in
`~/.local/share/knewstuff3/plasmoids.knsregistry`. Widgets installed by packages
or `kpackagetool6` aren't touched. Without any store widgets, the KDE Store isn't
contacted at all. A widget update takes effect once Plasma reloads it, e.g. after
logging in again.

RPM and Nix updates are shown as apps when the package ships an AppStream
desktop-application component (or, without any metainfo, a visible launcher).

## Commands

```text
ultramarine-updates list [--json]
ultramarine-updates apply [--yes]
ultramarine-updates settings [--json] [--automatic=BOOL] [--offline=BOOL]
                             [--inactive-minutes=N] [--window=HH:MM-HH:MM]
ultramarine-updates schedule [--cancel | --status [--json]]
ultramarine-updates run-scheduled [--tonight]   # system timers, as root
ultramarine-updates run-user [--tonight]        # user timers
```

`list --json` prints `{"updates": [...], "errors": [...], "restart_required": false, "offline_pending": false}`.
Each update has `id`, `name`, `source`, `backend` (`rpm`, `bootc`, `rpm-ostree`,
`flatpak`, `nix`, `plasmoid`), `kind` (`app`, `addon`, `runtime`, `package`),
`app_id` for apps, `old_version`, `new_version`, `icon`, `download_size` and
`installed_size_change` (bytes, 0 if unknown). A source that can't be queried
goes into `errors`; the others are still listed. `restart_required` is true when
a system image is staged, with its version in `staged_version` when known, or
when `offline_pending` is. `offline_pending` is true when RPM updates are
scheduled to install during the next restart (see below); they're still listed
as updates until then. If PackageKit's last offline update failed,
`offline_error` has PackageKit's message until updates are scheduled again or
packages change. It isn't counted in `errors`, so it doesn't make `list` fail.

## Offline updates

With "Install system updates while restarting" (`offline`), `apply`, the
automatic timers and Apply Tonight install RPM updates through PackageKit's
offline updates, so Plasma's logout screen offers "Install Updates and
Restart". As root:

1. `dnf upgrade --refresh --downloadonly --setopt=cachedir=/var/cache/PackageKit/<VERSION_ID>/metadata`
   downloads the metadata and packages into the cache PackageKit's dnf5 backend
   uses (`dnf5_setup_base()`; `VERSION_ID` from `/etc/os-release`). As root,
   libdnf5 uses `cachedir` (dnf only copies `system_cachedir` into it when it
   isn't set). Download and dependency problems are reported with dnf's
   messages.
2. `pkgcli refresh` makes a running `packagekitd` reread the metadata from that
   cache (without downloading it again; it otherwise keeps what it loaded
   earlier and finds no updates or old ones). Then
   `pkgcli --yes offline-update prepare` gets the updates, "downloads" them (they're already in the cache), records
   them in `/var/lib/PackageKit/prepared-update` and links `/system-update` to
   it (PackageKit's `Offline.Trigger`, action `reboot`). If PackageKit finds no
   updates, nothing is triggered. Failures are reported as "PackageKit could not
   prepare the update: ..." with pkgcli's error.
3. During the next restart, `packagekit-offline-update.service`
   (`pk-offline-update`) installs them and records the outcome in
   `/var/lib/PackageKit/offline-update-competed`. The cached packages are
   removed afterwards.

dnf runs to completion before PackageKit starts; downloading doesn't take the
rpm lock, and PackageKit queues its own transactions. Tested on Fedora 45
(PackageKit 1.4.0, dnf5 5.4.6): PackageKit prepared exactly the packages dnf
downloaded and wrote no package files of its own (none newer than the start of
step 2, same inodes), and `pk-offline-update` installed them.

Plasma's logout screen (plasma-workspace built with PackageKitQt) offers
"Install Updates and Restart" when `/system-update` links to
`/var/lib/PackageKit/prepared-update`; a plain Restart or Shut Down calls
PackageKit's `Offline.Cancel`, which removes `/system-update` but keeps the
prepared update. `list` reports `offline_pending` with the same check. The next
`apply` (or timer run) schedules it again.

Without PackageKit (`pkgcli`), `apply` fails with "Installing updates while
restarting needs PackageKit" instead of updating the running system. It also
refuses to replace another tool's `/system-update` (e.g. `dnf offline`) or a
PackageKit system upgrade, which PackageKit would otherwise overwrite.

Any rpm transaction (e.g. `dnf upgrade`) tells PackageKit through its rpm plugin,
and PackageKit then drops the prepared update and `/system-update`. Without a
`VERSION_ID` that's safe in a path, dnf uses its default cache, and PackageKit
downloads the packages again.

## Privileges

The KCM runs the helper as the logged-in user, so user Flatpaks, the Nix
profile and Plasma widgets are updated as that user. Commands that need root
(`apply`, changing `settings`, `schedule`) run their
system part first, as a child through `pkexec` (no terminal: a polkit prompt)
or `sudo` (in a terminal), and do the user's part only if that succeeded. So
if authentication is dismissed, nothing changes. pkexec's exit codes 126
(dismissed) and 127 (not authorized) are passed back to the KCM.

The polkit actions in `/usr/share/polkit-1/actions/org.ultramarine.updates.policy`
match the helper's exact install path, so the helper must be installed where
CMake's `ULTRAMARINE_UPDATES_EXECUTABLE` says (default
`/usr/libexec/ultramarine-updates`). pkexec clears the environment, so privileged
runs always use the real settings.

| Action | Command (`exec.argv1`) | Active session |
| --- | --- | --- |
| `org.ultramarine.updates.manage` | `apply` | administrator password, kept for a few minutes |
| `org.ultramarine.updates.settings` | `settings` | administrator password, kept |
| `org.ultramarine.updates.schedule` | `schedule` | administrator password, kept |

Every action names its command because pkexec (`find_action_for_path()` in
`src/programs/pkexec.c`) uses the first action whose `exec.path` matches and
whose `exec.argv1` matches if it's set, in the order polkitd's hash table
enumerates them, which is unspecified. An action matching only the path would
also match the other commands some of the time. The helper therefore puts the
command first when escalating.

Offline updates need no actions of their own: the helper talks to PackageKit as
root, and the logout screen uses PackageKit's
`org.freedesktop.packagekit.trigger-offline-update` (no password in the active
session) for "Install Updates and Restart" and cancelling.

## Automatic updates and Apply Tonight

Settings live in `/etc/ultramarine-updates/settings.json` (world-readable;
`ULTRAMARINE_UPDATES_CONFIG` overrides the path for development, and stops
`settings` from escalating):

```json
{ "automatic": true, "offline": false, "inactive_minutes": 30, "window_start": "", "window_end": "" }
```

- `ultramarine-updates-automatic.timer` (system, enabled by Ultramarine's systemd
  presets) runs
  `run-scheduled` every 5 minutes. It installs system updates once no one has
  used the device for `inactive_minutes`, inside the optional window, on AC power,
  at most once every 20 hours. After a failed install (often no network), it
  waits an hour before trying again.
- `ultramarine-updates-user.timer` (user, started by the
  `ultramarine-updates-user` autostart entry) runs `run-user`. With automatic
  updates on, it does the same for the user's Flatpaks, Nix profile and Plasma
  widgets. With them
  off ("Ask me"), it checks once a day and shows a notification with an
  "Open Updates" action.
- **Apply Tonight** (`schedule`) creates transient `ultramarine-updates-tonight`
  timers in the user and system managers. They install once tonight (22:00–06:00,
  or the configured window) while the device is unused, even with automatic
  updates off, then stop. A failed attempt is retried at most once an hour.

The user's updates also wait for AC power. "In use" means an active, unlocked,
non-idle `user` session of type wayland, x11, mir or tty (`loginctl`). Only
time the timers watched counts as unused: if more than 11 minutes pass between
two checks (the device was suspended or off), the count starts over.
Timestamps are kept in `/var/lib/ultramarine-updates` and
`~/.local/state/ultramarine-updates`. The device is never restarted
automatically.

Installs take a lock first, `/run/ultramarine-updates.lock` for system updates
(root only) and `$XDG_RUNTIME_DIR/ultramarine-updates.lock` for the user's, so
the automatic timers, Apply Tonight and `apply` never run dnf or flatpak at the
same time. A timer that finds the lock held skips that run; `apply` fails with
a message.

## Installed files

```text
/usr/libexec/ultramarine-updates
/usr/share/polkit-1/actions/org.ultramarine.updates.policy
/usr/lib/systemd/system/ultramarine-updates-automatic.{service,timer}
/usr/lib/systemd/user/ultramarine-updates-user.{service,timer}
/etc/xdg/autostart/ultramarine-updates-user.desktop
```

They're all in the `ultramarine-updates` CMake install component. Runtime
dependencies: polkit (`pkexec`), systemd, and `libnotify` (`notify-send`) for
"Ask me" notifications. dnf, flatpak, nix, bootc and rpm-ostree are used when
present. Offline updates need PackageKit 1.3.4 or later (`pkgcli`,
`packagekit-offline-update.service`) with its dnf5 backend. Plasma widgets need
KNewStuff (`kf6-knewstuff`), which the helper links against.
