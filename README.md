# Ultramarine Plasma KCMs

ultramarine plasma real

targeting **UM45**.

- `kcm-updates/` → `kcm_ultramarine_updates`: System Settings > Updates, backed by `um upgrade` (Automatic Updates and Apply Tonight use `um upgrade settings` / `um upgrade schedule`)
- `kcm-tweaks/` → `kcm_ultramarine_tweaks`: System Settings > Tweaks, backed by `um tweaks`
- `discover/`: patches that strip the updates UI out of Discover (see `discover/README.md`)

the plugins are named `kcm_ultramarine_*` because Discover already ships a `kcm_updates`.

## Runtime requirements

- `um` with `upgrade list --json` and `tweaks list --json` (installed at `/usr/bin/um` by default, override with `-DUM_EXECUTABLE=...`)
- `pkexec` (polkit). The KCMs run `um` as the user. When `um` needs root and has no terminal, it re-executes itself through `pkexec /usr/bin/um ...`, and umcli's `org.ultramarine.um.policy` (installed by the `umcli` package) allows that with `auth_admin_keep`. Running `um` unprivileged lets `upgrade apply` update user-installation Flatpaks before escalating. This needs the `pkexec` fallback in `um`'s `pkg/util/sudo.go`.

## Fedora dependencies

```bash
sudo dnf install \
    cmake \
    extra-cmake-modules \
    gcc-c++ \
    qt6-qtbase-devel \
    qt6-qtdeclarative-devel \
    qt6-qtquickcontrols2-devel \
    kf6-kconfig-devel \
    kf6-kcoreaddons-devel \
    kf6-kcmutils-devel \
    kf6-ki18n-devel \
    kf6-kirigami-devel
```

if you don't install these you might have a bad time

this project requires Qt 6.5 or newer and KDE Frameworks 6.0 or newer, as specified in the top-level `CMakeLists.txt`.

## Build with CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

to install for all users:

```bash
sudo cmake --install build
```

## Build with just

a `justfile` wraps the CMake commands. install [just](https://github.com/casey/just):

```bash
sudo dnf install just
```

then:

```bash
just              # configure (if needed) and build
just test         # build and run tests
sudo just install # install system-wide
just install-user # build and install into ~/.local
just clean        # remove build directories
```

override variables on the command line, e.g. a release build:

```bash
just build_type=Release configure
just build
```

to install to a user-local prefix instead:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

restart KDE Settings, or log out and back in if the modules do not appear immediately

## Zed and clangd

zed uses raw clangd and needs cmake compile commands. running these should fix it if you have any weird issues with headers not being found.

`just configure` generates the database and symlinks it to the repo root automatically.

or configure with CMake directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

then link the database:

```bash
ln -sf build/compile_commands.json compile_commands.json
```

if Zed still shows stale diagnostics, restart the language server or reopen the project after regenerating the database

## Testing in Plasma

the KCMs are installed as separate modules, so `Tweaks` and `Updates` should appear independently in System Settings (both under System).

to open one directly:

```bash
kcmshell6 kcm_ultramarine_updates
kcmshell6 kcm_ultramarine_tweaks
```

### Testing with a development build of umcli

the KCMs need a `um` that supports `--json` at `/usr/bin/um`, its tweaks in `/usr/share/um`, and umcli's polkit action. `pkexec` clears the environment, so the privileged side can't be pointed at a checkout. install it the way the RPM does:

```bash
just dev-um-install            # builds ../umcli (override with umcli_dir=...), uses sudo
just run tweaks                # opens the KCM from ./build without installing it
just run updates
just dev-um-uninstall          # removes /usr/bin/um, /usr/share/um and the polkit action
```

/usr/bin/um is preferred over `PATH`, so an old `um` in `~/go/bin` doesn't get in the way of the KCMs (it still wins in your shell).

for listing only, you can skip installing and put a dev build first in `PATH`, put it first in `PATH` (the KCMs only fall back to `PATH` when `/usr/bin/um` doesn't exist) and point it at the repo data:

```bash
PATH=/path/to/um-build-dir:$PATH UM_DATA=/path/to/umcli/data kcmshell6 kcm_ultramarine_tweaks
```

`pkexec` clears the environment, so `UM_DATA` doesn't reach privileged operations. Enabling or running a tweak always uses the tweaks in `/usr/share/um`.

## Troubleshooting package names

fedora package names can vary between Fedora releases and KDE packaging changes. if a package is unavailable, search the repositories for the corresponding development package:

```bash
dnf search kf6 kcmutils kirigami qt6
```

in particular, `kf6-kcmutils-devel` is the package that supplies the KCMUtils CMake integration and headers. installing it normally pulls in the required KDE Frameworks dependencies.
