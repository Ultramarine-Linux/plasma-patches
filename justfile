build_dir := "build"
build_type := "RelWithDebInfo"
prefix := "/usr"

default: build

# configure the cmake build directory
configure type=build_type pfx=prefix:
    cmake -S . -B {{ build_dir }} \
        -DCMAKE_BUILD_TYPE={{ type }} \
        -DCMAKE_INSTALL_PREFIX={{ pfx }}
    ln -sf {{ build_dir }}/compile_commands.json compile_commands.json

# build everything (configures first if needed)
build:
    @[ -f {{ build_dir }}/CMakeCache.txt ] || just build_dir={{ build_dir }} build_type={{ build_type }} prefix={{ prefix }} configure
    cmake --build {{ build_dir }} --parallel

# run the test suite
test: build
    ctest --test-dir {{ build_dir }} --output-on-failure

# install system-wide (run with sudo)
install:
    cmake --install {{ build_dir }}

# configure, build and install into ~/.local
install-user:
    just build_dir={{ build_dir }}-user configure {{ build_type }} "$HOME/.local"
    just build_dir={{ build_dir }}-user build
    cmake --install {{ build_dir }}-user

# remove build directories
clean:
    rm -rf {{ build_dir }} {{ build_dir }}-user compile_commands.json

# --- testing against a development build of umcli ---------------------------
# The KCMs need a um with `--json` support at /usr/bin/um, its tweak data in
# /usr/share/um, and umcli's polkit action. pkexec clears the environment, so
# UM_DATA can't point the privileged half at a checkout; install it like the RPM does.

umcli_dir := "../umcli"

# The update timers are installed but not enabled; enable them to try automatic updates.
# build umcli and install it system-wide like the umcli RPM (run as your user; uses sudo)
dev-um-install:
    cd {{ umcli_dir }} && go build -o /tmp/um-dev-build .
    sudo install -Dm0755 /tmp/um-dev-build /usr/bin/um
    sudo rm -rf /usr/share/um
    sudo mkdir -p /usr/share/um
    sudo cp -a {{ umcli_dir }}/data/. /usr/share/um/
    sudo install -Dm0644 {{ umcli_dir }}/polkit/org.ultramarine.um.policy /usr/share/polkit-1/actions/org.ultramarine.um.policy
    sudo install -Dm0644 -t /usr/lib/systemd/system {{ umcli_dir }}/systemd/system/*
    sudo install -Dm0644 -t /usr/lib/systemd/user {{ umcli_dir }}/systemd/user/*
    sudo systemctl daemon-reload
    systemctl --user daemon-reload
    @echo "installed $(/usr/bin/um --help >/dev/null && echo /usr/bin/um), /usr/share/um and the polkit action"

# remove what dev-um-install installed (keeps tweak state in /var/lib/um)
dev-um-uninstall:
    -sudo systemctl disable --now um-upgrade-automatic.timer
    -systemctl --user disable --now um-upgrade-user.timer
    sudo rm -f /usr/bin/um /usr/share/polkit-1/actions/org.ultramarine.um.policy
    sudo rm -f /usr/lib/systemd/system/um-upgrade-automatic.* /usr/lib/systemd/user/um-upgrade-user.*
    sudo systemctl daemon-reload
    sudo rm -rf /usr/share/um

# open a KCM from this build without installing it: just run tweaks | just run updates
run kcm: build
    #!/usr/bin/env bash
    set -euo pipefail
    plugins="$(find {{ build_dir }} -path '*/plasma/kcms/systemsettings/kcm_ultramarine_{{ kcm }}.so' -printf '%h\n' | head -1)"
    [ -n "$plugins" ] || { echo "kcm_ultramarine_{{ kcm }} not built" >&2; exit 1; }
    QT_PLUGIN_PATH="$(realpath "$plugins/../../..")${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
        kcmshell6 kcm_ultramarine_{{ kcm }}
