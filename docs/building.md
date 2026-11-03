# Build and test

## Dependencies

On Fedora or Ultramarine, install the Qt 6 and KDE Frameworks 6 development packages, CMake, Extra CMake Modules, a C++ compiler and gettext:

```bash
sudo dnf install \
    cmake extra-cmake-modules gcc-c++ gettext \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
    kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kcmutils-devel flatpak-devel \
    kf6-ki18n-devel kf6-kirigami-devel kf6-kio-devel kf6-kservice-devel kf6-kwidgetsaddons-devel kf6-knewstuff-devel \
    kf6-kirigami-addons lld
```

The project requires Qt 6.6 and KDE Frameworks 6.0 or newer.

- `flatpak-devel` (libflatpak) lets the office suite installer install from Flathub
  with a polkit prompt. KIO and KService start apps from the office suite installer
  and the dGPU launcher.
- `kf6-kirigami-addons` is only needed at runtime. KCM pages built with
  `org.kde.kirigamiaddons.formcard` (Location Services) fail to load without it.
- `lld` is optional but recommended. CMake links with it when it finds `ld.lld`,
  and falls back to the compiler's default linker otherwise. Pick a linker
  explicitly with `-DCMAKE_LINKER_TYPE=` (for example `BFD` or `MOLD`).

## Configure, build and test

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_CXX_FLAGS=-Werror
cmake --build build --parallel
ctest --test-dir build --output-on-failure
just cppcheck
```

The install paths of the update helper, its systemd units, the dGPU launcher,
the location helper and the office suite installer follow `CMAKE_INSTALL_PREFIX`.
Build directories configured before they did still have the old paths cached,
so reconfigure those with `cmake --fresh` (or `just clean`) before changing the
prefix.

The `justfile` also provides `just build`, `just test`, `just install`, and
`just install-user`. `just configure` generates `compile_commands.json` for
clangd.

`just install-user` installs under `~/.local`, which only works for the Updates and
Tweaks KCMs. The update helper, the location helper, their polkit actions, the
systemd units, the udev rule and the autostart entry are only read from system
locations, and the KCMs would run a user-writable helper through pkexec. Use
`just dev-updates-install` for the update helper. The Location Services switch
needs `/usr/libexec/ultramarine-location-helper` and its polkit policy installed
system-wide (`just install`).

To open the modules from the build without installing them:

```bash
just run updates
just run tweaks
just run location
```

## Test the update helper

The helper can list updates and show settings straight from the build:

```bash
just helper list --json
ULTRAMARINE_UPDATES_CONFIG=/tmp/settings.json just helper settings --automatic=false
```

The Updates KCM runs the helper from its install path, and the helper's polkit
action matches that exact path, so install it system-wide to test the page:

```bash
just dev-updates-install
just run updates
just dev-updates-uninstall
```

`dev-updates-install` installs only the `ultramarine-updates` CMake component:
the helper, its polkit action, systemd units and autostart entry. The automatic
update timer is not enabled; to try automatic updates, run
`sudo systemctl enable --now ultramarine-updates-automatic.timer` (with the
default settings, they install once the device has been unused for 5 minutes).
The page's Apply button actually installs updates. See
[Update helper](updates-backend.md) for behavior.

## Test with a development umcli

The Tweaks KCM expects `um` at `/usr/bin/um`, with tweak data in `/usr/share/um`.
From the `plasma-patches` checkout:

```bash
just dev-um-install
just run tweaks
just dev-um-uninstall
```

`dev-um-install` builds `../umcli` and installs the CLI, its data and its polkit
action for local testing. It uses `sudo` to change system files.
`dev-um-uninstall` removes those files. See [umcli backend](umcli-backend.md)
for behavior and limitations.

When testing the Tweaks page, enabling or running a tweak executes its Ansible
playbook/script with administrator privileges.
