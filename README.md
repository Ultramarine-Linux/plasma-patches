# Ultramarine Plasma KCMs

ultramarine plasma real

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

## Build with Meson

meson better than cmake

install Meson and Ninja:

```bash
sudo dnf install meson ninja-build
```

configure and build from the repository root:

```bash
meson setup meson-build --buildtype=debugoptimized
meson compile -C meson-build
```

the CMake backend build type can also be selected explicitly:

```bash
meson setup meson-build --wipe -Dcmake_build_type=Release
meson compile -C meson-build
```

install the resulting KCMs:

```bash
sudo meson install -C meson-build
```

for a user-local install, configure a prefix before compiling:

```bash
meson setup meson-build-user \
    --prefix="$HOME/.local" \
    --buildtype=debugoptimized
meson compile -C meson-build-user
meson install -C meson-build-user
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

configure with Meson:

```bash
meson setup meson-build --buildtype=debugoptimized
```

configure with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

for Meson, the database is inside the CMake backend directory. use the generated path shown by:

```bash
find meson-build -name compile_commands.json -print
```

for a direct CMake build:

```bash
ln -sf build/compile_commands.json compile_commands.json
```

if Zed still shows stale diagnostics, restart the language server or reopen the project after regenerating the database

## Testing in Plasma

the KCMs are installed as separate modules, so `Tweaks` and `Updates` should appear independently in System Settings.

## Troubleshooting package names

fedora package names can vary between Fedora releases and KDE packaging changes. if a package is unavailable, search the repositories for the corresponding development package:

```bash
dnf search kf6 kcmutils kirigami qt6
```

in particular, `kf6-kcmutils-devel` is the package that supplies the KCMUtils CMake integration and headers. installing it normally pulls in the required KDE Frameworks dependencies.
