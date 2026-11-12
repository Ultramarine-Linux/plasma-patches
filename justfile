build_dir := "build"
build_type := "RelWithDebInfo"
prefix := "/usr"

default: build

# configure the build directory, e.g. just configure Debug /usr -DCMAKE_CXX_FLAGS=-Werror
configure type=build_type pfx=prefix *args:
    cmake -S . -B {{ build_dir }} \
        -DCMAKE_BUILD_TYPE={{ type }} \
        -DCMAKE_INSTALL_PREFIX={{ pfx }} {{ args }}
    ln -sf {{ build_dir }}/compile_commands.json compile_commands.json

# build everything (configures first if needed)
build:
    @[ -f {{ build_dir }}/CMakeCache.txt ] || just build_dir={{ build_dir }} build_type={{ build_type }} prefix={{ prefix }} configure
    cmake --build {{ build_dir }} --parallel

# regenerate the translation template (needs gettext, python3 and polkit-devel)
translations:
    ./Messages.sh

# clang-format every tracked C++ file in place
format:
    git ls-files -z '*.cpp' '*.h' | xargs -0 -r clang-format -i

# fail if any tracked C++ file isn't clang-formatted
format-check:
    git ls-files -z '*.cpp' '*.h' | xargs -0 -r clang-format --dry-run --Werror

# run cppcheck like CI does (keep in sync with cpp-ci.yml and FyraLabs/actions cpp.yml)
cppcheck: build
    cppcheck --project={{ build_dir }}/compile_commands.json \
        --enable=warning,style,performance,portability \
        --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        --quiet \
        -j2 \
        --inline-suppr \
        --suppress='preprocessorErrorDirective:*_autogen*' \
        --library=qt \
        --library=cppcheck-kf6.cfg

# run the test suite
test: build
    ctest --test-dir {{ build_dir }} --output-on-failure

# install system-wide (run with sudo)
install:
    cmake --install {{ build_dir }}

# install the Updates and Tweaks KCMs into ~/.local (needs QT_PLUGIN_PATH, see docs/building.md)
install-user:
    cmake -S . -B {{ build_dir }}-user \
        -DCMAKE_BUILD_TYPE={{ build_type }} \
        -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
        -DULTRAMARINE_UPDATES_EXECUTABLE=/usr/libexec/ultramarine-updates \
        -DULTRAMARINE_LOCATION_HELPER=/usr/libexec/ultramarine-location-helper
    cmake --build {{ build_dir }}-user --parallel --target kcm_ultramarine_updates kcm_ultramarine_tweaks
    cmake --install {{ build_dir }}-user/kcm-updates
    cmake --install {{ build_dir }}-user/kcm-tweaks

# remove build directories
clean:
    rm -rf {{ build_dir }} {{ build_dir }}-user compile_commands.json

# --- testing the update helper ------------------------------------------------
# polkit matches the helper's install path, so it has to be installed system-wide.
# enable the timer by hand: sudo systemctl enable --now ultramarine-updates-automatic.timer

# install only the update helper component (uses sudo)
dev-updates-install: build
    sudo cmake --install {{ build_dir }} --component ultramarine-updates
    sudo systemctl daemon-reload
    systemctl --user daemon-reload

# remove what dev-updates-install installed (keeps settings in /etc/ultramarine-updates)
dev-updates-uninstall:
    -sudo systemctl disable --now ultramarine-updates-automatic.timer
    -sudo systemctl stop ultramarine-updates-tonight.timer
    -systemctl --user stop ultramarine-updates-user.timer ultramarine-updates-tonight-user.timer
    if [ -f {{ build_dir }}/install_manifest_ultramarine-updates.txt ]; then \
        xargs -d '\n' sudo rm -fv < {{ build_dir }}/install_manifest_ultramarine-updates.txt; fi
    sudo systemctl daemon-reload
    systemctl --user daemon-reload

# run the update helper from this build without installing it, e.g. just helper list --json
helper *args: build
    {{ build_dir }}/bin/ultramarine-updates {{ args }}

# --- testing against a development build of umcli ---------------------------
# pkexec clears the environment, so UM_DATA can't point at a checkout and um has to be installed

umcli_dir := "../umcli"

# build umcli and install it system-wide like the umcli RPM (run as your user, uses sudo)
dev-um-install:
    #!/usr/bin/env bash
    set -euo pipefail
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    (cd {{ umcli_dir }} && go build -o "$tmp/um" .)
    sudo install -Dm0755 "$tmp/um" /usr/bin/um
    sudo rm -rf /usr/share/um
    sudo mkdir -p /usr/share/um
    sudo cp -a {{ umcli_dir }}/data/. /usr/share/um/
    sudo install -Dm0644 {{ umcli_dir }}/polkit/org.ultramarine.um.policy /usr/share/polkit-1/actions/org.ultramarine.um.policy
    echo "installed $(/usr/bin/um --help >/dev/null && echo /usr/bin/um), /usr/share/um and the polkit action"

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
