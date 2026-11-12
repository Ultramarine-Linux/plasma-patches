# Updates KCM

the updates KCM uses its own C++ backend, `ultramarine-updates`, which lives in [`updater/`](../updater). it lists and installs updates from every source on the system and runs automatic updates and Apply Tonight. it replaces the `um upgrade` command that was proposed for umcli (umcli still backs the [tweaks KCM](umcli-backend.md)).

## the frontend

jade did most of the design and i did my best to match it in QML. "software to be updated" shows RPMs and flatpaks as proper apps (pretty name and icon), and system packages get tucked behind "show more" if there isn't room for them next to the apps. every update also shows what kind of package it is (RPM, flatpak, nix, plasmoid, etc.).

## what updates

| source | listing (as the user) | installing |
| --- | --- | --- |
| RPM packages | `dnf check-update`, sizes from `dnf repoquery` and `rpm` | `dnf upgrade --refresh` as root, or downloaded with dnf and installed on restart by packagekit (see [offline updates](#offline-updates)) |
| bootc / rpm-ostree images | whatever update bootc or rpm-ostree already found (`bootc status`, `rpm-ostree status`) | `bootc upgrade`, `um env update` on `um env` systems, or `rpm-ostree upgrade`, as root |
| flatpaks | `flatpak remote-ls --updates`, per installation | `flatpak update --user` as the user, `--system` as root |
| nix profile | `nix profile list` plus an evaluation (`nix eval --impure`, nothing gets built) of what `nix profile upgrade` would install | `nix profile upgrade --all --impure`, as the user |
| determinate nix | `determinate-nixd version` | `determinate-nixd upgrade`, as root |
| plasmoids | KNewStuff (`plasmoids.knsrc`) compares the user's registry with the KDE store | KNewStuff installs the new version, as the user |

RPM and nix updates show up as apps when the package ships an appstream desktop-application component (or, with no metainfo at all, a visible launcher).

### bootc

on bootc systems (`/run/ostree-booted`), dnf isn't touched anywhere. the image update always gets staged for the next boot, so turning off offline updates doesn't do anything (you can't update the image live) and the KCM just shows it as on.

once the image update is staged, it's no longer listed as an update. `list` reports `restart_required` instead, and the KCM offers to restart through plasma's logout screen. download-only stages (`bootc upgrade --download-only`) aren't applied when restarting, so they're still listed.

neither tool can check the registry without root, so an image update only shows up once bootc or rpm-ostree has found one. the system timer runs `bootc upgrade --check` (or `rpm-ostree upgrade --check`) every 6 hours for that, whether or not automatic updates are on. `um env` systems track a local image, so their base image updates only show up when installing.

`bootc status` needs root too on a booted system (it enters a mount namespace first). so the system timer, `bootc upgrade --check` and installing all keep a world-readable copy of its output in `/var/lib/ultramarine-updates/bootc-status.json`, tagged with the boot ID, and the listing reads that as the user. a copy from a previous boot gets ignored. without a current copy, or when bootc doesn't track an image (`spec.image` is null), the listing falls back to `rpm-ostree status`.

the helper never restarts the device by itself. don't enable bootc's `bootc-fetch-apply-updates.timer` alongside it, since that runs `bootc upgrade --apply`, which reboots as soon as an update is staged.

### plasmoids

these use KNewStuff on the backend, so it's only the widgets installed from the KDE store through "Get New Widgets" (or discover), which KNewStuff records in `~/.local/share/knewstuff3/plasmoids.knsregistry`. anything installed through a package or `kpackagetool6` isn't handled through KNewStuff and isn't touched. with no store widgets, the KDE store isn't contacted at all.

an update takes effect once plasma reloads the widget, like after logging in again. if one widget takes more than 10 minutes to install, the rest get skipped until the next run, because KNewStuff can't cancel the one that's still going.

### nix

the listing evaluates with `--impure`, like `nix profile upgrade --impure`, so unlocked flake references resolve the same way and nix's `tarball-ttl` decides when they get fetched again. each profile element is evaluated on its own. one that fails (unfree or insecure without being allowed, or gone from its flake) gets left out instead of failing the whole listing.

## offline updates

KDE doesn't handle offline updates with dnf well, so i made the backend hand them to packagekit. dnf does the downloading and checking, and packagekit only prepares and installs what's already in its cache. what's nice about this is that plasma cancels the offline update if a user picks a plain restart or shut down! there's a separate option to install updates and restart, so updates never install when users don't want them to.

with "Install system updates while restarting" (`offline`) on, `apply`, the automatic timers and Apply Tonight do this as root:

1. `dnf upgrade --refresh --downloadonly --setopt=cachedir=/var/cache/PackageKit/<VERSION_ID>/metadata` downloads the metadata and packages into the cache packagekit's dnf5 backend uses (`dnf5_setup_base()`, with `VERSION_ID` from `/etc/os-release`). download and dependency problems get reported with dnf's messages
2. `pkgcli refresh` makes a running `packagekitd` reread the metadata from that cache without downloading it again (otherwise it keeps what it loaded earlier and finds no updates, or old ones). then `pkgcli --yes offline-update prepare` "downloads" the packages (they're already in the cache), records them in `/var/lib/PackageKit/prepared-update` and links `/system-update` to it, so the system sees an offline update is prepared. if packagekit finds no updates, nothing gets triggered
3. on the next restart, `packagekit-offline-update.service` installs them and records how it went in `/var/lib/PackageKit/offline-update-competed`. the cached packages get removed afterwards

dnf finishes before packagekit starts. downloading doesn't take the rpm lock, and packagekit queues its own transactions. i tested this on fedora 45 (packagekit 1.4.0, dnf5 5.4.6): packagekit prepared exactly the packages dnf downloaded, didn't write any package files of its own, and `pk-offline-update` installed them.

plasma's logout screen (plasma-workspace built with PackageKitQt) offers "Install Updates and Restart" when `/system-update` links to `/var/lib/PackageKit/prepared-update`. a plain restart or shut down calls packagekit's `Offline.Cancel`, which removes `/system-update` but keeps the prepared update. `list` reports `offline_pending` with the same check, and the next `apply` (or timer run) schedules it again.

a few edge cases:

- without packagekit (`pkgcli`), `apply` fails with "Installing updates while restarting needs PackageKit" instead of updating the running system
- it won't replace another tool's `/system-update` (like `dnf offline`) or a packagekit system upgrade, which packagekit would otherwise overwrite
- any rpm transaction (like `dnf upgrade`) pokes packagekit through its rpm plugin, and packagekit then drops the prepared update and `/system-update`
- without a `VERSION_ID` that's safe in a path, dnf uses its default cache and packagekit downloads the packages again

## privileges

the KCM runs the helper as the logged-in user, because user flatpaks, the nix profile and plasmoids have to be updated as that user. anything that needs root (`apply`, changing `settings`, `schedule`) runs its system part first, as a child process through `pkexec` (a polkit prompt, for the GUI) or `sudo` (if you run the helper in a terminal), and then the user's part.

the user's part only runs if the root part got past authentication, so updates don't get half-installed when you dismiss the prompt. after that, `apply` still installs the user's updates if some system updates failed, while `schedule` skips the user's part if the system part failed for any reason. pkexec's 126 (dismissed) and 127 (not authorized) get passed back to the KCM. the root part exits with 2 when it fails, so `sudo`'s own 1 (wrong password, or not in sudoers) can be told apart and gets treated like pkexec's 127.

the polkit policy gets installed to `/usr/share/polkit-1/actions/org.ultramarine.updates.policy` and matches the helper's exact install path. this matters for installation: the helper **must** be installed where `ULTRAMARINE_UPDATES_EXECUTABLE` says in CMake (default `/usr/libexec/ultramarine-updates`).

| action | command (`exec.argv1`) | active session |
| --- | --- | --- |
| `org.ultramarine.updates.manage` | `apply` | admin password, kept for a few minutes |
| `org.ultramarine.updates.settings` | `settings` | admin password, kept |
| `org.ultramarine.updates.schedule` | `schedule` | admin password, kept |

every action names its command because pkexec (`find_action_for_path()` in `src/programs/pkexec.c`) picks the first action whose `exec.path` matches (and `exec.argv1`, if set) in polkitd's hash table order, which is unspecified. an action matching only the path would sometimes match the other commands too. that's also why the helper puts the command first when escalating.

applying offline updates needs no actions of its own. the helper talks to packagekit as root, and the logout screen uses packagekit's `org.freedesktop.packagekit.trigger-offline-update` (no password in the active session) for "Install Updates and Restart" and canceling.

pkexec clears the environment, so runs escalated through it (the KCM) always use the real settings and state. in a terminal, sudo gets `ULTRAMARINE_UPDATES_CONFIG` and `ULTRAMARINE_UPDATES_STATE` on its command line, so a development override applies to the root part too. `schedule` passes them on to the transient tonight units (`systemd-run --setenv`). changing `settings` with `ULTRAMARINE_UPDATES_CONFIG` set doesn't escalate at all.

the KCM starts the helper with stdin on `/dev/null`, and the helper does the same for the tools it only reads from, so nothing ever waits for an answer. the helper and the package managers it runs ignore `SIGPIPE`, so closing the KCM mid-install makes their progress output fail instead of killing dnf or flatpak halfway.

## commands

```text
ultramarine-updates list [--json]
ultramarine-updates apply [--yes]
ultramarine-updates settings [--json] [--automatic=BOOL] [--offline=BOOL]
                             [--inactive-minutes=N] [--window=HH:MM-HH:MM]
ultramarine-updates schedule [--cancel | --status [--json]]   # --json only with --status
ultramarine-updates run-scheduled [--tonight]   # system timers, as root
ultramarine-updates run-user [--tonight]        # user timers
```

### `list --json`

prints `{"updates": [...], "errors": [...], "restart_required": false, "offline_pending": false}`. each update has `id`, `name`, `source`, `backend` (`rpm`, `bootc`, `rpm-ostree`, `flatpak`, `nix`, `plasmoid`), `kind` (`app`, `addon`, `runtime`, `package`), `app_id` for apps, `old_version`, `new_version`, `icon`, `download_size` and `installed_size_change` (bytes, 0 if unknown).

- a source that can't be queried goes into `errors` and the others still get listed. `list` only exits with 1 if every source it queried failed
- `restart_required` is true when a system image is staged (with its version in `staged_version` when known) or when `offline_pending` is
- `offline_pending` is true when RPM updates are scheduled to install on the next restart. they're still listed as updates until then
- `offline_error` has packagekit's message if its last offline update failed, until updates get scheduled again or packages change. it doesn't count as an error, so `list` doesn't fail because of it

### `schedule --status --json`

prints `{"scheduled": true, "next": 1761181200}` (`next` is a unix timestamp, left out when unknown). run as the user, it checks both halves of Apply Tonight: `scheduled` is only true if the system's and the user's timers are both active, and `"partial": true` gets added when only one is (see [Apply Tonight](#apply-tonight)). run as root, it only checks the system's timer.

### `apply`

`apply` and the timers run every package manager even if an earlier one failed, and report all the failures at the end. the only ordering is inside RPM offline updates, where packagekit only prepares the update if dnf's download worked. run as the user, `apply` exits with the system part's exit code if that failed.

a successful `apply` gets recorded like an automatic install (`last-automatic-update` in the state directories, for the system's and the user's part separately). it also stops that part's Apply Tonight timer, since tonight would have nothing left to do, and the automatic timer then waits its 20 hours. the KCM checks the schedule again after installing.

## automatic updates

settings live in `/etc/ultramarine-updates/settings.json` (world-readable). `ULTRAMARINE_UPDATES_CONFIG` overrides the path for development and stops `settings` from escalating, and `ULTRAMARINE_UPDATES_STATE` overrides the system state directory (`/var/lib/ultramarine-updates`) the same way. if the file is broken, the timers and `apply` warn and use the defaults until it's fixed or overwritten by saving settings, so automatic updates and the image check keep going. the defaults:

```json
{ "automatic": true, "offline": false, "inactive_minutes": 5, "window_start": "", "window_end": "" }
```

### the system timer

`ultramarine-updates-automatic.timer` (enabled by Ultramarine's systemd presets) runs `run-scheduled` every 5 minutes. it installs system updates once nobody has used the device for `inactive_minutes`, inside the optional window, on AC power, at most once every 20 hours. after a failed install (usually no network) it waits an hour before trying again.

an install counts as done, and the next one waits 20 hours, once the main system update (RPMs or the image) worked, even if another source failed (say a flatpak remote that's down). that failure still gets reported (the service fails), and the failed source just gets retried with the next regular install instead of everything every hour. without a main system update (the user's timer), a run counts as done once any source worked.

### the user timer

`ultramarine-updates-user.timer` (started by the `ultramarine-updates-user` autostart entry) runs `run-user`. with automatic updates on, it does the same thing for the user's flatpaks, nix profile and plasmoids. with them off ("Ask me"), it checks once a day and shows a notification with an "Open Updates" action. a check where every source failed (like no network right after logging in) doesn't count, so the next run tries again.

`notify-send` blocks until the notification is acted on or closed, so it runs in its own transient user service (which also opens system settings) instead of holding up `run-user`. without `systemd-run`, the helper waits for it for at most 4 minutes.

### Apply Tonight

`schedule` creates a transient `ultramarine-updates-tonight` timer in the system manager and `ultramarine-updates-tonight-user` in the user's. they install once tonight (22:00–06:00, or the configured window) while the device is unused, even with automatic updates off, then stop.

- a failed attempt gets retried at most once an hour. a partial failure still counts as installed, so the timer stops instead of retrying every night
- if the automatic timer or a manual `apply` already installed updates after Apply Tonight was scheduled (recorded in `tonight-scheduled` in the state directories), the tonight timer stops without installing again
- the system's tonight service waits for `network-online.target`, like the automatic one
- scheduling again replaces the timers, unless tonight's service is installing right now, which gets left to finish

Apply Tonight is _partial_ when only one of its two timers is still active, which happens a lot. each half stops its own timer once it's installed (or found the automatic timer already did), and the other might follow minutes later or only the next night. the user's half also lives in the user's systemd manager, which stops at logout (unless lingering is on) and takes the timer with it. run as the user, `schedule --status` reports `"scheduled": false, "partial": true` in both cases, so the KCM doesn't claim everything installs tonight. scheduling again sets up both halves, and `schedule --cancel` stops whatever's left.

### when the device counts as in use

the user's updates wait for AC power too. the device is in use while:

- a `user` session of type wayland, x11, mir or tty is active, unlocked and not idle (`loginctl`), so inactivity counts from when logind sets the session's `IdleHint` or `LockedHint`
- an app holds a logind `idle` block inhibitor (`ListInhibitors`, like `systemd-inhibit --what=idle mpv`) for a user with such a session, except powerdevil's (it only blocks the power keys and the lid) and our own
- an app keeps the screen on through plasma, like browsers and video players do with `org.freedesktop.ScreenSaver.Inhibit`

that last one takes extra work. kscreenlocker hands those inhibitions to powerdevil's PolicyAgent (`ChangeScreenSettings`), which keeps them to itself: it never adds them to logind, and its `IdleHint` ignores them. so `run-user` asks `org.kde.Solid.PowerManagement.PolicyAgent.HasInhibition(4)` on the session bus, and while that's true, writes a timestamp to `$XDG_RUNTIME_DIR/ultramarine-updates/screen-inhibited` (in "Ask me" mode too, for the system's Apply Tonight).

it only asks while one of the user's sessions is active, because powerdevil reports every inhibition as held in a session that isn't in the foreground (`requirePolicyCheck()`), which would hold off updates as long as a switched-away session stays logged in.

the system timer can't reach the session bus, so it treats a marker less than 11 minutes old in `/run/user/<uid>/ultramarine-updates/` of a user with an active session as in use. that directory belongs to the user, so root opens it and the marker by descriptor without following symlinks (`O_NOFOLLOW`), checks both are owned by the user, and only reads a small regular file. a FIFO or device never gets read and can't block it. the worst a user can do with the marker is hold off updates, which an app that never releases its inhibition can do anyway.

plasma doesn't set `IdleHint` by itself ([KDE bug 448942](https://bugs.kde.org/show_bug.cgi?id=448942)). our backport of powerdevil MR !699 (see [powerdevil](powerdevil.md)) sets it after 5 minutes without input. without it, counting only starts once the screen locks, by default after 5 minutes.

only time the timers actually watched counts as unused. if more than 11 minutes pass between two checks (the device was suspended or off), the count starts over. when a check first finds the device unused, it counts from the sessions' `IdleSinceHint` if all of them are idle, but never from before the previous check, which saw the device in use.

that's why `inactive_minutes` defaults to 5. powerdevil suspends after 15 idle minutes on AC by default, and the checks run every 5 to 6 minutes. with the `IdleHint` backport, inactivity starts at minute 5 and reaches 5 counted minutes at the first check after minute 10, which almost always beats the suspend at 15. without it, the count starts at the first check after the lock (minute 5 to 11), reaches 5 minutes between minute 10 and 16, and about half of idle periods end in suspend first. a longer wait like 30 never gets reached on a default install that suspends.

timestamps are kept in `/var/lib/ultramarine-updates` and `~/.local/state/ultramarine-updates`.

### inhibitors, timeouts and locks

while installing, `apply` and the timers hold a logind `shutdown:sleep` block inhibitor, so plasma's logout screen and idle suspend wait for them. if logind refuses (polkit might not allow it for the user timer, which isn't part of a session), the install goes ahead with a warning.

`ultramarine-updates-automatic.service` and the system's tonight service have `TimeoutStopSec=30min`, because rpm finishes its transaction before handling `SIGTERM` and the default 90 seconds could cut that short. they also have `TimeoutStartSec=6h` (oneshot services otherwise never time out), so a run stuck on a stalled download can't keep the timer from starting it again. the user services have `TimeoutStartSec=6h` too.

installs take a lock first: `/run/ultramarine-updates.lock` for system updates (root only) and `$XDG_RUNTIME_DIR/ultramarine-updates.lock` for the user's. so the automatic timers, Apply Tonight and `apply` never install system updates twice at once, and the same goes for each user's own. the locks are separate, so system and user updates can install at the same time (each package manager guards its own installation). listing takes no lock, so the KCM's `dnf check-update` can run during an install. a timer that finds the lock held skips that run, and `apply` fails with a message.

the bootc status cache has its own lock, `/run/ultramarine-updates-status.lock`, so refreshing it never turns an install away. the system timer's image check holds the system lock for the whole check (and skips it while an install runs), so `rpm-ostree upgrade --check` never runs alongside an install. while checking it also holds `/run/ultramarine-updates-image-check.lock`. `apply` waits up to 2 seconds for the system lock, and if the check lock is held too, it says so and waits up to 15 minutes for the check to finish instead of giving up.

## translations

messages the KCM shows (progress, failures, the notification and the reason given to logind's inhibitor) are translated in the `ultramarine-plasma-settings` domain. the `--help` text and the plain-text `settings` output are for the command line and aren't. pkexec keeps `LANG` and `LC_*`, so the root part uses the same language.

## installed files

```text
/usr/libexec/ultramarine-updates
/usr/share/polkit-1/actions/org.ultramarine.updates.policy
/usr/lib/systemd/system/ultramarine-updates-automatic.{service,timer}
/usr/lib/systemd/user/ultramarine-updates-user.{service,timer}
/etc/xdg/autostart/ultramarine-updates-user.desktop
```

they're all in the `ultramarine-updates` CMake install component. runtime dependencies are polkit (`pkexec`), systemd, and `libnotify` (`notify-send`) for "Ask me" notifications. dnf, flatpak, nix, bootc and rpm-ostree get used when they're there. offline updates need packagekit 1.3.4 or later (`pkgcli`, `packagekit-offline-update.service`) with its dnf5 backend. plasmoids need KNewStuff (`kf6-knewstuff`), which the helper links against.

## what's left

- major upgrades aren't handled yet. that can wait until after um45 comes out, since um44 doesn't have this either
- more review, especially of automatic updates
- a copy pass on the KCM
