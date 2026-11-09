# Launch Using Dedicated Graphics Card

On hybrid GPU systems, the app launchers (Kickoff, Kicker, Application
Dashboard) show **Launch Using Dedicated Graphics Card** in each app's context
menu. That includes laptops with an eGPU plugged in.

## How it works

- **The action:** Plasma's launchers load drop-in actions from
  `share/plasma/kickeractions/*.desktop` for every app, passing the app's desktop
  file as `%u` ([plasma-workspace!1355](https://invent.kde.org/plasma/plasma-workspace/-/merge_requests/1355)).
  [`../dgpu-launch/data/ultramarine-dgpu-launch.desktop.in`](../dgpu-launch/data/ultramarine-dgpu-launch.desktop.in)
  runs `/usr/libexec/ultramarine-dgpu-launch %u`. `%u` is the app's desktop file as a
  local path (kicker's `handleAdditionalAppActions()` passes `QUrl::fromLocalFile(entryPath)`).
- **The launcher:** [`../dgpu-launch/main.cpp`](../dgpu-launch/main.cpp) reads the
  discrete GPU's environment (`DRI_PRIME`, NVIDIA PRIME offload variables,
  `VK_LOADER_DRIVERS_SELECT`) from switcheroo-control's `GPUs` D-Bus property. It
  takes the first discrete GPU that isn't the default, then any non-default GPU, like
  KIO does for `PrefersNonDefaultGPU`. If switcheroo-control isn't running, it does what
  switcheroo-control would: when `/sys/module/nvidia` exists, the NVIDIA PRIME offload
  variables (`__NV_PRIME_RENDER_OFFLOAD`, `__GLX_VENDOR_LIBRARY_NAME`, `__VK_LAYER_NV_optimus`),
  since the proprietary driver ignores `DRI_PRIME`. Otherwise `DRI_PRIME=1`. Then it expands the app's `Exec` line with `KIO::DesktopExecParser`
  and runs it with `KIO::CommandLauncherJob`, which starts it in its own `app-*.service`
  unit with the launcher's activation token, like Plasma does.
- **Why not `kioclient exec` or `switcherooctl launch`:** KIO starts apps with
  `DBusActivatable=true` (most GNOME apps, some KDE ones) by D-Bus activation, so the
  app is started by the D-Bus daemon and never sees the environment. That's how
  `kioclient exec` and KIO's own `PrefersNonDefaultGPU` handling work, so those apps
  would silently run on the integrated GPU. `CommandLauncherJob` never uses D-Bus
  activation.
- **Task manager:** Plasma only reads drop-ins in the launchers, so the task
  manager doesn't show the action. That's waiting on an upstream plasma-desktop
  change; Ultramarine ships Fedora's plasma-desktop unpatched.
- **Only on hybrid systems:** `/usr/share/plasma/kickeractions/ultramarine-dgpu-launch.desktop`
  is a symlink to `/run/ultramarine-dgpu-launch/ultramarine-dgpu-launch.desktop`.
  `ultramarine-dgpu-detect` creates that link only when the default (`boot_vga`)
  GPU isn't discrete and another GPU is. Discrete means switcheroo-control's
  `switcheroo-discrete-gpu` udev tag. Otherwise the symlink is broken, and Plasma
  skips broken symlinks: it lists the directory with `QDir::Files`.
- **Hotplug:** `90-ultramarine-dgpu.rules` restarts `ultramarine-dgpu-detect.service`
  when a DRM card is added or removed, which covers coldplug at boot and plugging in or
  removing an eGPU. The menus are built each time they open, so changes show up
  without restarting Plasma.

| Setup                                        | Shown |
| -------------------------------------------- | ----- |
| Laptop, iGPU + dGPU                          | yes   |
| Laptop + eGPU                                | yes   |
| Desktop, single dGPU                         | no    |
| Desktop, iGPU enabled but display on the dGPU | no    |
| Two dGPUs, no iGPU                           | no    |
| iGPU only                                    | no    |

## Packaging

The detect service has no `[Install]` section; only the udev rule starts it. After
a fresh install, nothing runs it until the next reboot or GPU hotplug, so the
action doesn't show up. The package's `%post` must reload the udev rules and run
the check once:

```spec
Requires: switcheroo-control
%{?systemd_requires}
BuildRequires: systemd-rpm-macros

%post
%udev_rules_update
# Run the check now instead of waiting for a reboot or hotplug.
systemctl start ultramarine-dgpu-detect.service >/dev/null 2>&1 || :
```

`udevadm trigger --subsystem-match=drm --action=add` works instead of `systemctl start`,
because it replays the rule for every card. It also re-runs other packages' DRM
rules, so starting the service is the safer choice.

Runtime dependencies:

- `switcheroo-control`: `switcheroo-control.service`, which the launcher asks for the
  GPUs' environment over D-Bus (Fedora's presets enable it), and the
  `switcheroo-discrete-gpu` udev tag (detection). `switcherooctl` isn't used.

Build dependencies are the KF6 KIO, KService, I18n and WidgetsAddons development
packages the rest of the tree already needs.

## Limits

- Apps whose desktop file has no `Exec` line (allowed for `DBusActivatable` apps,
  though rare) can't be started this way, and show an error.
- Single-instance apps that are already running (Firefox, Discord, most Electron
  apps) hand off to the running copy, so they stay on the GPU they started on.
- Flatpak apps get the environment through `flatpak run`, so this works as long
  as the app has GPU access (`--device=dri`).
- Users can hide the action by placing a file named `ultramarine-dgpu-launch.desktop` with no
  `Actions=` in `~/.local/share/plasma/kickeractions/`. User files shadow system
  files with the same name. Plasma lists the `kickeractions` directories only once,
  the first time a launcher menu opens (a `static` in kicker's `additionalActions()`),
  so if `~/.local/share/plasma/kickeractions/` didn't exist yet, restart plasmashell
  (`systemctl --user restart plasma-plasmashell`) after creating it. Files added to a
  directory Plasma already knows about are picked up the next time a menu opens.

## Testing

```bash
# Is this machine hybrid? (exit 0 = shown, 1 = hidden; changes nothing)
/usr/libexec/ultramarine-dgpu-detect --check

# The GPUs and environments switcheroo-control knows about:
switcherooctl list

# What the action runs, by hand. Try a DBusActivatable app too (grep -l DBusActivatable=true
# /usr/share/applications/*.desktop), which kioclient exec would start without the environment.
/usr/libexec/ultramarine-dgpu-launch /usr/share/applications/org.kde.kwrite.desktop
tr '\0' '\n' < /proc/$(pgrep -n kwrite)/environ | grep -E 'DRI_PRIME|NV_PRIME|VK_LOADER'
```

`tests/test_dgpu_launch.cpp` covers picking the GPU from switcheroo-control's list.

Plasma looks up `share/plasma/kickeractions` the first time a launcher menu opens.
After the first install, restart plasmashell once.
