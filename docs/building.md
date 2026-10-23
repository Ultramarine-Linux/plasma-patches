# Build and test

## Dependencies

On Fedora or Ultramarine, install the Qt 6 and KDE Frameworks 6 development packages, CMake, Extra CMake Modules, a C++ compiler and gettext:

```bash
sudo dnf install \
    cmake extra-cmake-modules gcc-c++ gettext \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
    kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kcmutils-devel \
    kf6-ki18n-devel kf6-kirigami-devel
```

The project requires Qt 6.5 and KDE Frameworks 6.0 or newer.

## Configure, build and test

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS=-Werror
cmake --build build --parallel
ctest --test-dir build --output-on-failure
just cppcheck
```

The `justfile` also provides `just build`, `just test`, `just install`, and
`just install-user`. `just configure` generates `compile_commands.json` for
clangd.

To install a development build of the modules without installing them system-wide:

```bash
just run updates
just run tweaks
```

## Test with a development umcli

The KCMs expect `um` at `/usr/bin/um`, with tweak data in `/usr/share/um`. From
the `plasma-patches` checkout:

```bash
just dev-um-install
just run updates
just run tweaks
just dev-um-uninstall
```

`dev-um-install` builds `../umcli` and installs the CLI, its data and its systemd
units for local testing. It does **not** enable the timers. It does use `sudo` to
change system files. `dev-um-uninstall` removes those files. See
[umcli backend](umcli-backend.md) for behavior and limitations.

When testing the Tweaks page, enabling or running a tweak executes its Ansible
playbook/script with administrator privileges. The Updates page's Apply button
actually installs updates.
