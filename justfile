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

# regenerate the gettext POT templates for Weblate (requires gettext-tools)
translations:
    ./Messages.sh

# run cppcheck with the same checks as CI; suppress only false positives in generated Qt moc files
cppcheck: build
    cppcheck -j2 --project={{ build_dir }}/compile_commands.json \
        --enable=warning,style,performance,portability \
        --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        --suppress='preprocessorErrorDirective:*_autogen*' \
        --quiet

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

# --- testing the update helper ------------------------------------------------
# The Updates KCM runs ultramarine-updates from its install path, and its polkit
# action matches that exact path, so the helper must be installed system-wide.
# The automatic update timer is enabled statically; the user timer starts with the
# next graphical session (or: systemctl --user start ultramarine-updates-user.timer).

# install just the update helper, its polkit action, systemd units and autostart entry (uses sudo)
dev-updates-install: build
    sudo cmake --install {{ build_dir }} --component ultramarine-updates
    sudo systemctl daemon-reload
    systemctl --user daemon-reload

# remove what dev-updates-install installed (keeps settings in /etc/ultramarine-updates)
dev-updates-uninstall:
    -sudo systemctl stop ultramarine-updates-automatic.timer ultramarine-updates-tonight.timer
    -systemctl --user stop ultramarine-updates-user.timer ultramarine-updates-tonight-user.timer
    [ -f {{ build_dir }}/install_manifest_ultramarine-updates.txt ] && \
        xargs -d '\n' sudo rm -fv < {{ build_dir }}/install_manifest_ultramarine-updates.txt
    sudo systemctl daemon-reload
    systemctl --user daemon-reload

# run the update helper from this build without installing it, e.g. just helper list --json
helper *args: build
    {{ build_dir }}/bin/ultramarine-updates {{ args }}

# --- testing against a development build of umcli ---------------------------
# The Tweaks KCM needs a um with `--json` support at /usr/bin/um, its tweak data in
# /usr/share/um, and umcli's polkit action. pkexec clears the environment, so
# UM_DATA can't point the privileged half at a checkout; install it like the RPM does.

umcli_dir := "../umcli"

# build umcli and install it system-wide like the umcli RPM (run as your user; uses sudo)
dev-um-install:
    cd {{ umcli_dir }} && go build -o /tmp/um-dev-build .
    sudo install -Dm0755 /tmp/um-dev-build /usr/bin/um
    sudo rm -rf /usr/share/um
    sudo mkdir -p /usr/share/um
    sudo cp -a {{ umcli_dir }}/data/. /usr/share/um/
    sudo install -Dm0644 {{ umcli_dir }}/polkit/org.ultramarine.um.policy /usr/share/polkit-1/actions/org.ultramarine.um.policy
    @echo "installed $(/usr/bin/um --help >/dev/null && echo /usr/bin/um), /usr/share/um and the polkit action"

# remove what dev-um-install installed (keeps tweak state in /var/lib/um)
dev-um-uninstall:
    sudo rm -f /usr/bin/um /usr/share/polkit-1/actions/org.ultramarine.um.policy
    sudo rm -rf /usr/share/um

# open a KCM from this build without installing it: just run tweaks | just run updates
run kcm: build
    #!/usr/bin/env bash
    set -euo pipefail
    plugins="$(find {{ build_dir }} -path '*/plasma/kcms/systemsettings/kcm_ultramarine_{{ kcm }}.so' -printf '%h\n' | head -1)"
    [ -n "$plugins" ] || { echo "kcm_ultramarine_{{ kcm }} not built" >&2; exit 1; }
    QT_PLUGIN_PATH="$(realpath "$plugins/../../..")${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" \
        kcmshell6 kcm_ultramarine_{{ kcm }}
