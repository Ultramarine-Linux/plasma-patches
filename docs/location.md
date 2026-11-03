# Location Services

`kcm_ultramarine_location` adds **Security & Privacy → Location Services** to System
Settings: one switch that lets apps ask GeoClue for the device's location, or stops them.
Plasma has no global switch for this yet. [plasma-workspace!5434](https://invent.kde.org/plasma/plasma-workspace/-/merge_requests/5434)
only sets a manual position, and the design is still being discussed in
[systemsettings#44](https://invent.kde.org/plasma/systemsettings/-/work_items/44). Drop this
KCM once Plasma ships one.

## How it works

GeoClue is started over D-Bus whenever an app asks for it, so disabling `geoclue.service`
does nothing. Turning location off masks it instead. Taidan's Slint rewrite (its
[`slint` branch](https://github.com/Ultramarine-Linux/taidan/tree/slint), not on `main` yet)
masks it the same way when location is turned off during setup, so both will agree on the
state. Masking is also what [KDE bug 515145](https://bugs.kde.org/show_bug.cgi?id=515145)
proposes Plasma check to grey out Night Light's and the time zone's automatic location.

- **State:** read from systemd's manager over D-Bus with `GetUnitFileState("geoclue.service")`,
  which needs no authentication. `masked` and `masked-runtime` are off, anything else
  (normally `static`) is on. A missing unit means GeoClue isn't installed: systemd 259
  answers `org.freedesktop.DBus.Error.FileNotFound` for that, and `org.freedesktop.systemd1.NoSuchUnit`
  is accepted too. Any other error shows an error with a Try Again button rather than
  claiming GeoClue isn't installed.
- **Changes:** `pkexec /usr/libexec/ultramarine-location-helper enable|disable`. The helper
  (`kcm-location/data/ultramarine-location-helper.in`) is a small root shell script that
  only accepts those two words:
  - `disable` runs `systemctl mask --force --now geoclue.service`, so apps that already
    have a location stop getting updates too.
  - `enable` runs `systemctl unmask` and `systemctl unmask --runtime`, so a mask in `/etc`
    or `/run` is removed.

  `systemctl` reloads the manager by itself. The helper's polkit action,
  `org.ultramarine.location.manage` (`org.ultramarine.location.policy`), matches the helper's
  exact path and is `auth_admin_keep`, so a change asks for the password once.
  pkexec's exit codes 126 and 127 mean authentication was cancelled or failed and nothing
  changed (the helper never exits with those itself). Any other failure says the change may
  have been partly applied. The state is read again after every change.

The helper's path follows the install prefix. Set `-DULTRAMARINE_LOCATION_HELPER=...` to
change it. The polkit action is generated with the same path.

It's system-wide, for every user. GNOME has its own per-user switch
(`org.gnome.system.location`), which this doesn't touch.

## Runtime requirements

- `geoclue2`, otherwise the page says location services aren't installed.
- systemd, `pkexec` and a polkit authentication agent.
- `kirigami-addons` (the page uses `org.kde.kirigamiaddons.formcard`), which packaging has
  to `Require`.
- The helper and its polkit action, installed with the KCM to `libexec` and
  `share/polkit-1/actions`. polkit only reads actions from `/usr/share/polkit-1/actions`.
