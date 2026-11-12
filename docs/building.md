# Building and testing

## dependencies

you need qt 6.6 and KDE frameworks 6.0 or newer. on fedora or ultramarine:

```bash
sudo dnf install \
    cmake extra-cmake-modules gcc-c++ gettext python3 polkit-devel \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
    kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kcmutils-devel flatpak-devel \
    kf6-ki18n-devel kf6-kirigami-devel kf6-kio-devel kf6-kservice-devel kf6-kwidgetsaddons-devel kf6-knewstuff-devel \
    kf6-kirigami-addons lld
```

- `flatpak-devel` (libflatpak) is for the office installer, so it can install from flathub with a polkit prompt. KIO and KService start apps from the office installer and the dGPU launcher
- `gettext`, `python3` and `polkit-devel` merge translations into the desktop files, polkit policies and KCM metadata at build time (`polkit-devel` ships the gettext ITS rules for polkit policies)
- `kf6-kirigami-addons` is only needed at runtime. the location KCM uses `org.kde.kirigamiaddons.formcard` and won't load without it
- `lld` is optional. CMake links with it when it finds `ld.lld` and uses the compiler's default linker otherwise. pick one yourself with `-DCMAKE_LINKER_TYPE=` (like `BFD` or `MOLD`)

## configure, build and test

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_CXX_FLAGS=-Werror
cmake --build build --parallel
ctest --test-dir build --output-on-failure
just cppcheck
```

or just use the `justfile`: `just build`, `just test`, `just install` and `just install-user`. `just configure` generates `compile_commands.json` for clangd and passes extra arguments on to CMake (`just configure Debug /usr -DCMAKE_CXX_FLAGS=-Werror`).

the install paths of the update helper, its systemd units, the dGPU launcher, the location helper and the office installer all follow `CMAKE_INSTALL_PREFIX`. the systemd unit and udev rules directories come from `systemd.pc` and `udev.pc` with `/usr` swapped for the prefix (falling back to `lib/systemd/{system,user}` and `lib/udev/rules.d` under it). you can set them with `-DSYSTEMD_SYSTEM_UNIT_DIR=`, `-DSYSTEMD_USER_UNIT_DIR=` and `-DUDEV_RULES_DIR=`. these paths are cached, so reconfigure with `cmake --fresh` (or `just clean`) before changing the prefix of an existing build directory.

## running the KCMs

to open the modules straight from the build without installing them:

```bash
just run updates
just run tweaks
just run location
```

### installing into `~/.local`

`just install-user` builds the updates and tweaks KCMs in `build-user` and installs only those under `~/.local`. the helpers, polkit actions, systemd units, udev rule and autostart entry are only read from system locations, and polkit only matches the helpers' system paths, so the updates KCM is built to run `/usr/libexec/ultramarine-updates`. install that with `just dev-updates-install` (or the RPM).

the location KCM isn't installed this way, since its switch needs `/usr/libexec/ultramarine-location-helper` and its polkit policy installed system-wide. use `just install` for that one.

qt doesn't look for plugins under `~/.local`, so system settings only finds the KCMs with `QT_PLUGIN_PATH` set (the desktop files and translations in `~/.local/share` get found without it):

```bash
QT_PLUGIN_PATH="$HOME/.local/lib64/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
    systemsettings kcm_ultramarine_updates
```

to have the whole plasma session pick them up, export it from a script in `~/.config/plasma-workspace/env/` and log in again.

## testing the update helper

the helper runs straight from the build:

```bash
just helper list --json
ULTRAMARINE_UPDATES_CONFIG=/tmp/settings.json just helper settings --automatic=false
```

the updates KCM runs the helper from its install path, and the polkit action matches that exact path, so install it system-wide to test the page:

```bash
just dev-updates-install
just run updates
just dev-updates-uninstall
```

`dev-updates-install` only installs the `ultramarine-updates` CMake component: the helper, its polkit action, the systemd units and the autostart entry. the automatic update timer isn't enabled. to try automatic updates, run `sudo systemctl enable --now ultramarine-updates-automatic.timer` (with the default settings, updates install once the device has been unused for 5 minutes).

heads up: the page's Apply button installs real updates. see [updates KCM](updates-backend.md) for how it all behaves.

## testing with a development umcli

the tweaks KCM expects `um` at `/usr/bin/um`, with tweak data in `/usr/share/um`. from the `plasma-patches` checkout:

```bash
just dev-um-install
just run tweaks
just dev-um-uninstall
```

`dev-um-install` builds `../umcli` and installs the CLI, its data and its polkit action with `sudo`, and `dev-um-uninstall` removes them. see [tweaks KCM](umcli-backend.md) for how it behaves.

heads up again: enabling or running a tweak from the page runs its ansible playbook or script as root on your machine.
