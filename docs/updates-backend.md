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
| Nix profile | `nix profile list` and an evaluation (`nix eval --impure`, nothing is built) of what `nix profile upgrade` would install | `nix profile upgrade --all --impure`, as the user |
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
the listing reads as the user. Writing it makes sure the directory can be
listed by everyone, whoever created it and with whatever umask (pkexec keeps
the caller's). A copy from a previous boot is ignored, and
without one, or on hosts where bootc doesn't track an image (`spec.image` is
null), the listing falls back to `rpm-ostree status`.

Images update through this helper, which never restarts the device. Don't enable
bootc's `bootc-fetch-apply-updates.timer` alongside it: it runs
`bootc upgrade --apply`, which restarts as soon as an update is staged.

Plasma widgets are the ones installed from the KDE Store through "Get New
Widgets" (or Discover), which KNewStuff records in
`~/.local/share/knewstuff3/plasmoids.knsregistry`. Widgets installed by packages
or `kpackagetool6` aren't touched. Without any store widgets, the KDE Store isn't
contacted at all. A widget update takes effect once Plasma reloads it, e.g. after
logging in again. If a widget takes longer than 10 minutes to install, the rest are
skipped until the next run, since KNewStuff can't cancel the one still going.

The Nix listing evaluates with `--impure`, like `nix profile upgrade --impure`,
so unlocked flake references resolve the same way, and Nix's `tarball-ttl`
decides when they're fetched again. Each profile element is evaluated on its
own: one that fails (unfree or insecure without being allowed, or gone from
its flake) is left out instead of failing the whole listing, and outputs the
package doesn't have anymore are skipped.

RPM and Nix updates are shown as apps when the package ships an AppStream
desktop-application component (or, without any metainfo, a visible launcher).

## Commands

```text
ultramarine-updates list [--json]
ultramarine-updates apply [--yes]
ultramarine-updates settings [--json] [--automatic=BOOL] [--offline=BOOL]
                             [--inactive-minutes=N] [--window=HH:MM-HH:MM]
ultramarine-updates schedule [--cancel | --status [--json]]   # --json only with --status
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
`list` exits with 1 only if every source it queried failed.

`schedule --status --json` prints `{"scheduled": true, "next": 1761181200}`
(`next` is a Unix timestamp, left out when unknown). Run as the user, it checks
both halves of Apply Tonight: `scheduled` is true only if the system's and the
user's timers are both active, and `"partial": true` is added when only one of
them is (see [Automatic updates and Apply Tonight](#automatic-updates-and-apply-tonight)).
Run as root, it only checks the system's timer.

`apply` and the timers run every package manager even if an earlier one failed,
and report all failures at the end (see [Automatic updates and Apply
Tonight](#automatic-updates-and-apply-tonight) for what the timers do after a
partial failure). The only ordering is within RPM offline
updates: PackageKit prepares the update only if dnf's download worked. Run as
the user, `apply` installs the user's updates even if the system part failed
(but not if authentication was dismissed or refused), and exits with the system
part's exit code if that failed. The root part exits with 2 when it fails, so
`sudo`'s own 1 (a wrong password, or not allowed to use sudo) can be told apart
and is treated like pkexec's 127.

A successful `apply` is recorded like an automatic install
(`last-automatic-update` in the state directories, for the system's and the
user's part separately) and stops that part's Apply Tonight timer, since
tonight would have nothing left to install. The automatic timer then also waits
its 20 hours. The KCM checks the schedule again after installing.

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
or `sudo` (in a terminal), and then the user's part. If authentication is
dismissed or refused (with sudo: it exits with 1), the user's part is skipped, so nothing changes. `apply`
still installs the user's updates if some system updates failed. `schedule`
skips the user's part if the system part failed for any reason. pkexec's exit codes 126
(dismissed) and 127 (not authorized) are passed back to the KCM. The KCM starts
the helper with stdin on `/dev/null`, and the helper does the same for the
tools it only reads from, so nothing can wait forever for an answer.

The polkit actions in `/usr/share/polkit-1/actions/org.ultramarine.updates.policy`
match the helper's exact install path, so the helper must be installed where
CMake's `ULTRAMARINE_UPDATES_EXECUTABLE` says (default
`/usr/libexec/ultramarine-updates`). pkexec clears the environment, so runs
escalated through it (the KCM) always use the real settings and state. sudo (in
a terminal) is passed `ULTRAMARINE_UPDATES_CONFIG` and `ULTRAMARINE_UPDATES_STATE`
on its command line, so a development override also applies to the root part
there. `schedule` passes them on to the transient tonight units too
(`systemd-run --setenv`), which otherwise start with a clean environment.
Changing `settings` with `ULTRAMARINE_UPDATES_CONFIG` set doesn't escalate
at all.

The helper ignores `SIGPIPE`, and so do the package managers it runs, so closing
the KCM mid-install makes their progress output fail instead of killing dnf or
flatpak halfway.

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
session) for "Install Updates and Restart" and canceling.

## Automatic updates and Apply Tonight

Settings live in `/etc/ultramarine-updates/settings.json` (world-readable;
`ULTRAMARINE_UPDATES_CONFIG` overrides the path for development, and stops
`settings` from escalating). If the file is broken, the timers and `apply`
warn and use the defaults until it's fixed (or replaced by saving settings),
so automatic updates and the image check keep going. The defaults:

```json
{ "automatic": true, "offline": false, "inactive_minutes": 5, "window_start": "", "window_end": "" }
```

`ULTRAMARINE_UPDATES_STATE` overrides the system state directory
(`/var/lib/ultramarine-updates`) for development, the same way.

- `ultramarine-updates-automatic.timer` (system, enabled by Ultramarine's systemd
  presets) runs
  `run-scheduled` every 5 minutes. It installs system updates once no one has
  used the device for `inactive_minutes`, inside the optional window, on AC power,
  at most once every 20 hours. After a failed install (often no network), it
  waits an hour before trying again. An install counts as done (and the next
  one waits 20 hours) once the main system update, RPMs or the image, worked,
  even if another source failed, say a Flatpak remote that's down. That
  failure is still reported (the service fails), and the failed source is
  retried with the next regular install instead of everything every hour.
  Without a main system update (the user's timer), it counts as done once any
  source worked.
- `ultramarine-updates-user.timer` (user, started by the
  `ultramarine-updates-user` autostart entry) runs `run-user`. With automatic
  updates on, it does the same for the user's Flatpaks, Nix profile and Plasma
  widgets. With them
  off ("Ask me"), it checks once a day and shows a notification with an
  "Open Updates" action. A check where every source failed (no network yet
  right after logging in) isn't counted, so the next run tries again.
  `notify-send` waits until the notification is acted on or closed, so it runs
  in a transient user service of its own (which also opens System Settings)
  instead of holding up `run-user`. Without `systemd-run`, the helper waits for
  it at most 4 minutes.
- **Apply Tonight** (`schedule`) creates a transient
  `ultramarine-updates-tonight` timer in the system manager and
  `ultramarine-updates-tonight-user` in the user's. They install once tonight
  (22:00–06:00, or the configured window) while the device is unused, even with
  automatic updates off, then stop. A failed attempt is retried at most once an
  hour. A partial failure (see above) still counts as installed, so the timer
  stops instead of retrying every night. If the automatic timer (or a manual `apply`) already installed updates after Apply Tonight was
  scheduled (recorded in `tonight-scheduled` in the state directories), the
  tonight timer stops without installing again. The system's tonight service
  waits for `network-online.target`, like the automatic one. Scheduling again
  replaces the timers, except while tonight's service is installing, which is
  left to finish.

Apply Tonight is _partial_ when only one of its two timers is still active.
That's common: each half stops its own timer once it installed (or found the
automatic timer already did), and the other can follow minutes later or only
the next night. The user's half also lives in the user's systemd manager, which
stops when they log out (unless lingering is enabled), taking the timer with it,
so only the system's updates are installed tonight. Run as the user,
`schedule --status` reports `"scheduled": false, "partial": true` in both
cases, so the KCM doesn't claim everything installs tonight. Scheduling again
sets up both halves, and `schedule --cancel` stops whichever is left (a timer
that isn't loaded anymore isn't an error).

The user's updates also wait for AC power. The device is in use while:

- a `user` session of type wayland, x11, mir or tty is active, unlocked and not
  idle (`loginctl`), so inactivity counts from when logind sets the session's
  `IdleHint` or `LockedHint`;
- an app holds a logind `idle` block inhibitor (`ListInhibitors`; e.g.
  `systemd-inhibit --what=idle mpv`) for a user with such a session, except
  PowerDevil's (it only blocks the power keys and the lid) and our own;
- or an app keeps the screen on through Plasma, as browsers and video players do
  with `org.freedesktop.ScreenSaver.Inhibit`. kscreenlocker passes those to
  PowerDevil's PolicyAgent (`ChangeScreenSettings`), which keeps them to itself:
  PowerDevil only reads logind's inhibitors, never adds these to logind, and
  its `IdleHint` ignores them. So `run-user` asks
  `org.kde.Solid.PowerManagement.PolicyAgent.HasInhibition(4)` on the session
  bus, and while it's true writes a timestamp to
  `$XDG_RUNTIME_DIR/ultramarine-updates/screen-inhibited` (also with "Ask me",
  for the system's Apply Tonight). It only asks while one of the user's
  sessions is active: PowerDevil reports every inhibition as held in a session
  that isn't in the foreground (`requirePolicyCheck()`), which would otherwise
  hold off updates for as long as a switched-away session stays logged in. The
  system timer, which can't reach the session bus, treats a marker less than 11
  minutes old in `/run/user/<uid>/ultramarine-updates/` of a user with an
  active session as in use. That directory is the user's, so root opens it and
  the marker by descriptor without following symlinks (`O_NOFOLLOW`), checks
  that both are the user's, and only reads a small regular file: a FIFO or
  device is never read and can't block it. A user can only use the marker to
  hold off updates, and an app that never releases its inhibition does that
  too.

Plasma doesn't set `IdleHint` by itself ([KDE bug
448942](https://bugs.kde.org/show_bug.cgi?id=448942)). Our backport of
powerdevil MR !699 sets it after 5 minutes without input. Without it, counting
only starts once the screen locks, by default after 5 minutes. Only time the
timers watched counts as unused: if more than 11 minutes pass between two
checks (the device was suspended or off), the count starts over. When a check
first finds the device unused, it counts from the sessions' `IdleSinceHint`
(when logind's idle hint was set) if all of them are idle, but never from
before the previous check, which saw the device in use.

That's why `inactive_minutes` defaults to 5. PowerDevil suspends after 15 idle
minutes on AC by default, and the checks run every 5 to 6 minutes. With the
`IdleHint` backport, inactivity starts at minute 5 and reaches 5 counted minutes
at the first check after minute 10, which nearly always comes before the
suspend at 15. Without it, the count starts at the first check after the lock
(minute 5 to 11), reaches 5 minutes between minute 10 and 16, and about half of
the idle periods end in suspend first. A longer wait like 30 is never reached
on a default install that suspends. Timestamps are kept in
`/var/lib/ultramarine-updates` and `~/.local/state/ultramarine-updates`. The
device is never restarted automatically.

While installing, `apply` and the timers hold a logind `shutdown:sleep` block
inhibitor (`org.freedesktop.login1.Manager.Inhibit`), so Plasma's logout screen
and idle suspend wait for them. If logind refuses (polkit may not allow it for
the user timer, which isn't part of a session), the install goes ahead with a
warning. `ultramarine-updates-automatic.service` and the system's tonight
service have `TimeoutStopSec=30min`: rpm finishes its transaction before
handling `SIGTERM`, and the default 90 seconds could cut that short. They also
have `TimeoutStartSec=6h` (oneshot services otherwise never time out), so a run
stuck on a stalled download can't keep the timer from starting it again. That's
stopped the same way, so a running rpm transaction still finishes. The user's
`ultramarine-updates-user.service` and tonight's user service have
`TimeoutStartSec=6h` too.

Installs take a lock first, `/run/ultramarine-updates.lock` for system updates
(root only) and `$XDG_RUNTIME_DIR/ultramarine-updates.lock` for the user's. So
the automatic timers, Apply Tonight and `apply` never install system updates
twice at the same time, and the same goes for each user's own. The two locks are
separate, so the system's and a user's updates can install at the same time
(each package manager guards its own installation). Listing takes no lock, so
the KCM's `dnf check-update` can run during an install. A timer that finds the
lock held skips that run; `apply` fails with a message. The bootc status cache
has a lock of its own, `/run/ultramarine-updates-status.lock`, so refreshing it
never turns away an install. The system timer's check for a new image holds the
system lock for the whole check (and skips it while an install runs), so
`rpm-ostree upgrade --check` never runs alongside an install. While checking, it
also holds `/run/ultramarine-updates-image-check.lock`: `apply` waits up to 2
seconds for the system lock, and if the check lock is held too, it says so and
waits up to 15 minutes for the check to finish instead of giving up.

## Translations

Messages the KCM shows (progress, failures, the notification and the reason
given to logind's inhibitor) are translated in the `ultramarine-plasma-settings`
domain, like the KCMs. The `--help` text and the plain-text `settings` output
are for the command line and aren't. pkexec keeps `LANG` and `LC_*`, so the
root part speaks the same language.

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
