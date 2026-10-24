# Update helper (`ultramarine-updates`)

The Updates KCM is a frontend for `ultramarine-updates`, a small C++/QtCore
helper in [`updater/`](../updater). It lists and installs updates from every
source on the system and runs automatic updates and Apply Tonight. It replaces
the `um upgrade` command proposed for umcli; umcli still backs the Tweaks KCM
(see [umcli backend](umcli-backend.md)).

## What it updates

| Source | Listing (as the user) | Installing |
| --- | --- | --- |
| RPM packages | `dnf check-update`, sizes from `dnf repoquery` and `rpm` | `dnf upgrade --refresh` (with `--offline` if enabled), as root |
| Image mode (bootc/rpm-ostree) | the update bootc/rpm-ostree already found (`bootc status`, `rpm-ostree status`) | `bootc upgrade`, `um env update` on `um env` systems, or `rpm-ostree upgrade`, as root |
| Flatpak | `flatpak remote-ls --updates`, per installation | `flatpak update --user` as the user, `--system` as root |
| Nix profile | `nix profile list` and a pure evaluation of what `nix profile upgrade` would install | `nix profile upgrade --all --impure`, as the user |
| Determinate Nix | `determinate-nixd version` | `determinate-nixd upgrade`, as root |

On image mode systems (`/run/ostree-booted`), dnf is never used. The image
update is staged for the next boot either way, so the offline setting doesn't
apply there. Neither tool can check the registry without root, so the listing
shows an image update only after bootc or rpm-ostree found one on their own.

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

`list --json` prints `{"updates": [...], "errors": [...]}`. Each update has
`id`, `name`, `source`, `backend` (`rpm`, `bootc`, `rpm-ostree`, `flatpak`,
`nix`), `kind` (`app`, `runtime`, `package`), `app_id` for apps, `old_version`,
`new_version`, `icon`, `download_size` and `installed_size_change` (bytes, 0 if
unknown). A source that can't be queried goes into `errors`; the others are
still listed.

## Privileges

The KCM runs the helper as the logged-in user, so user Flatpaks and the Nix
profile are updated as that user. Commands that need root (`apply`, changing
`settings`, `schedule`) do the user's part first and then re-execute through
`pkexec` (no terminal: a polkit prompt) or `sudo` (in a terminal). pkexec's exit
codes 126 (dismissed) and 127 (not authorized) are passed back to the KCM.

The polkit action `org.ultramarine.updates.manage`
(`/usr/share/polkit-1/actions/org.ultramarine.updates.policy`) matches the
helper's exact install path, so the helper must be installed where CMake's
`ULTRAMARINE_UPDATES_EXECUTABLE` says (default `/usr/libexec/ultramarine-updates`).
pkexec clears the environment, so privileged runs always use the real settings.

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
  at most once every 20 hours.
- `ultramarine-updates-user.timer` (user, started by the
  `ultramarine-updates-user` autostart entry) runs `run-user`. With automatic
  updates on, it does the same for the user's Flatpaks and Nix profile. With them
  off ("Ask me"), it checks once a day and shows a notification with an
  "Open Updates" action.
- **Apply Tonight** (`schedule`) creates transient `ultramarine-updates-tonight`
  timers in the user and system managers. They install once tonight (22:00–06:00,
  or the configured window) while the device is unused, even with automatic
  updates off, then stop.

"In use" means an active, unlocked, non-idle `user` session of type wayland,
x11, mir or tty (`loginctl`). Timestamps are kept in
`/var/lib/ultramarine-updates` and `~/.local/state/ultramarine-updates`. The
device is never restarted automatically.

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
present.
