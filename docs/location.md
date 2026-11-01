# Location Services

`kcm_ultramarine_location` adds **Security & Privacy → Location Services** to System
Settings: one switch that lets apps ask GeoClue for the device's location, or stops them.
Plasma has no global switch for this yet. [plasma-workspace!5434](https://invent.kde.org/plasma/plasma-workspace/-/merge_requests/5434)
only sets a manual position, and the design is still being discussed in
[systemsettings#44](https://invent.kde.org/plasma/systemsettings/-/work_items/44). Drop this
KCM once Plasma ships one.

## How it works

GeoClue is started over D-Bus whenever an app asks for it, so disabling `geoclue.service`
does nothing. Turning location off masks it instead, which is also what
[Taidan](https://github.com/Ultramarine-Linux/taidan) does when location is turned off during
setup, so both agree on the state. Masking is also what [KDE bug 515145](https://bugs.kde.org/show_bug.cgi?id=515145)
proposes Plasma check to grey out Night Light's and the time zone's automatic location.

The KCM talks to systemd's manager over D-Bus, with no helper of its own:

- **State:** `GetUnitFileState("geoclue.service")`. `masked` and `masked-runtime` are off,
  `NoSuchUnit` means GeoClue isn't installed, and anything else (normally `static`) is on.
- **Off:** `MaskUnitFiles`, `Reload`, then `StopUnit`, so apps that already have a location
  stop getting updates.
- **On:** `UnmaskUnitFiles` (from `/run` if the mask was a runtime one), then `Reload`.

These calls allow interactive authorization, so systemd asks polkit
(`org.freedesktop.systemd1.manage-unit-files`, `reload-daemon` and `manage-units`). Those
are `auth_admin_keep`, so turning location off normally asks for the password once.

It's system-wide, for every user. GNOME has its own per-user switch
(`org.gnome.system.location`), which this doesn't touch.

## Runtime requirements

- `geoclue2`, otherwise the page says location services aren't installed.
- systemd and a polkit authentication agent.
