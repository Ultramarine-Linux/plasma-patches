# dGPU launch

this one's pretty simple. plasma's launchers (kickoff, kicker, application dashboard) read drop-in actions from `/usr/share/plasma/kickeractions/*.desktop` for every app, and pass the app's desktop file as `%u` ([plasma-workspace!1355](https://invent.kde.org/plasma/plasma-workspace/-/merge_requests/1355)). we use that to drop in an `ultramarine-dgpu-launch.desktop` that adds **Launch Using Dedicated Graphics Card** to every app's context menu and launches it on the dGPU.

it only shows up if the system has both an iGPU and a dGPU **and** the display is on the iGPU. a system that runs its display on the dGPU (like a typical desktop with the iGPU still enabled) won't show the action. eGPUs on laptops count too.

| setup                                         | shown |
| --------------------------------------------- | ----- |
| laptop, iGPU + dGPU                           | yes   |
| laptop + eGPU                                 | yes   |
| desktop, single dGPU                          | no    |
| desktop, iGPU enabled but display on the dGPU | no    |
| two dGPUs, no iGPU                            | no    |
| iGPU only                                     | no    |

## how it works

### the action

[`dgpu-launch/data/ultramarine-dgpu-launch.desktop.in`](../dgpu-launch/data/ultramarine-dgpu-launch.desktop.in) runs `/usr/libexec/ultramarine-dgpu-launch %u`. `%u` is the app's desktop file as a local path (kicker's `handleAdditionalAppActions()` passes `QUrl::fromLocalFile(entryPath)`).

### the launcher

[`dgpu-launch/main.cpp`](../dgpu-launch/main.cpp) asks switcheroo-control for the dGPU's environment (`DRI_PRIME`, the NVIDIA PRIME offload variables, `VK_LOADER_DRIVERS_SELECT`) through its `GPUs` D-Bus property. it picks the first discrete GPU that isn't the default, then any non-default GPU, same as KIO does for `PrefersNonDefaultGPU`.

if switcheroo-control isn't running, it does what switcheroo-control would: the NVIDIA PRIME offload variables when `/sys/module/nvidia` exists (the proprietary driver ignores `DRI_PRIME`), otherwise `DRI_PRIME=1`.

then it expands the app's `Exec` line with `KIO::DesktopExecParser` and runs it with `KIO::CommandLauncherJob`, which starts it in its own `app-*.service` unit with the launcher's activation token, just like plasma does. since this skips KIO's "are you sure" prompt, it refuses any desktop file KIO would ask about (`KDesktopFile::isAuthorizedDesktopFile()`).

### why not `switcherooctl launch` or `kioclient exec`?

KIO starts `DBusActivatable=true` apps (most GNOME apps, some KDE ones) through D-Bus activation, so the D-Bus daemon starts the app and it never sees our environment. `kioclient exec` and KIO's own `PrefersNonDefaultGPU` handling both work that way, so those apps would silently end up on the iGPU. `CommandLauncherJob` never uses D-Bus activation.

### only on hybrid systems

`/usr/share/plasma/kickeractions/ultramarine-dgpu-launch.desktop` is a symlink to `/run/ultramarine-dgpu-launch/ultramarine-dgpu-launch.desktop`. `ultramarine-dgpu-detect` only creates that target when the default (`boot_vga`) GPU isn't discrete and another GPU is ("discrete" meaning switcheroo-control's `switcheroo-discrete-gpu` udev tag). otherwise the symlink is broken, and plasma skips broken symlinks since it lists the directory with `QDir::Files`.

### hotplug

`90-ultramarine-dgpu.rules` restarts `ultramarine-dgpu-detect.service` whenever a DRM card is added or removed, which covers coldplug at boot and plugging in or yanking an eGPU. the menus get built every time they open, so changes show up without restarting plasma.

## can we make this work elsewhere?

yes we can! for now it's only in kicker (and the other launchers), but it's possible to get it into task manager and icon-only task manager too. we'd have to patch `plasma-desktop` and possibly `plasma-workspace` though. why am i not doing that? i don't want to vendor `plasma-desktop`. what am i doing instead? i've submitted merge requests to KDE ([plasma-desktop!4149](https://invent.kde.org/plasma/plasma-desktop/-/merge_requests/4149) and [plasma-workspace!7333](https://invent.kde.org/plasma/plasma-workspace/-/merge_requests/7333)) that make drop-in context menu actions show up in task manager! they won't land until around plasma 6.9 though.

## packaging

the detect service has no `[Install]` section, only the udev rule starts it. so after a fresh install nothing runs it until the next reboot or GPU hotplug, and the action doesn't show up. `%post` needs to reload the udev rules and run the check once:

```spec
Requires: switcheroo-control
%{?systemd_requires}
BuildRequires: systemd-rpm-macros

%post
%udev_rules_update
systemctl start ultramarine-dgpu-detect.service >/dev/null 2>&1 || :
```

`udevadm trigger --subsystem-match=drm --action=add` would work too, but it re-runs every other package's DRM rules, so starting the service is safer.

the only runtime dependency is `switcheroo-control`: the launcher talks to `switcheroo-control.service` over D-Bus (fedora's presets enable it) and detection uses its udev tag. `switcherooctl` itself isn't used.

## limits

- apps with no `Exec` line (allowed for `DBusActivatable` apps, but rare) can't be launched this way and show an error
- single-instance apps that are already running (firefox, discord, most electron apps) hand off to the running copy, so they stay on whatever GPU they started on
- flatpaks get the environment through `flatpak run`, so they work as long as the app has GPU access (`--device=dri`)
- users can hide the action by putting an `ultramarine-dgpu-launch.desktop` with no `Actions=` in `~/.local/share/plasma/kickeractions/`, since user files shadow system ones. plasma only lists the `kickeractions` directories the first time a launcher menu opens (it's a `static` in kicker's `additionalActions()`), so if that directory didn't exist yet, restart plasmashell after creating it

## testing

```bash
# is this machine hybrid? (exit 0 = shown, 1 = hidden, changes nothing)
/usr/libexec/ultramarine-dgpu-detect --check

# what switcheroo-control knows about
switcherooctl list

# run the action by hand. try a DBusActivatable app too
# (grep -l DBusActivatable=true /usr/share/applications/*.desktop)
/usr/libexec/ultramarine-dgpu-launch /usr/share/applications/org.kde.kwrite.desktop
tr '\0' '\n' < /proc/$(pgrep -n kwrite)/environ | grep -E 'DRI_PRIME|NV_PRIME|VK_LOADER'
```

`tests/test_dgpu_launch.cpp` covers picking the GPU from switcheroo-control's list. restart plasmashell once after the first install, since plasma looks up the `kickeractions` directories the first time a launcher menu opens.
