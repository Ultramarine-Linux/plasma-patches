#!/usr/bin/bash
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# Runs ultramarine-dgpu-detect against fake /sys/class/drm trees, using a
# fake udevadm that reports the switcheroo-discrete-gpu tag.
set -euo pipefail

detect=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/bin"
cat >"$tmp/bin/udevadm" <<'UDEVADM'
#!/usr/bin/bash
path=${*: -1}
cat "${path#--path=}/fake-udev" 2>/dev/null
UDEVADM
chmod +x "$tmp/bin/udevadm"

# card <setup> <name> <boot_vga> <discrete>
card() {
    local dir=$tmp/$1/$2
    mkdir -p "$dir/device" "$tmp/$1/$2-eDP-1"
    echo "$3" >"$dir/device/boot_vga"
    if [[ $4 == 1 ]]; then
        echo "TAGS=:seat:switcheroo-discrete-gpu:" >"$dir/fake-udev"
    else
        echo "TAGS=:seat:" >"$dir/fake-udev"
    fi
}

card laptop card0 1 0
card laptop card1 0 1
card egpu card0 1 0
card egpu card2 0 1
card desktop card0 1 1
card desktop-igpu card0 0 0
card desktop-igpu card1 1 1
card igpu-only card0 1 0
card two-dgpus card0 1 1
card two-dgpus card1 0 1
mkdir -p "$tmp/empty"

failed=0
expect() {
    local setup=$1 want=$2 link=$tmp/run/$1/action.desktop
    export PATH=$tmp/bin:$PATH ULTRAMARINE_DGPU_DRM_DIR=$tmp/$setup ULTRAMARINE_DGPU_LINK=$link

    local got=hidden
    if "$detect" --check >/dev/null; then got=shown; fi

    # Without --check it makes or removes the link. Start from a stale link.
    mkdir -p "${link%/*}"
    ln -sfn /nonexistent "$link"
    "$detect"
    local linked=hidden
    if [[ -L $link ]]; then linked=shown; fi

    if [[ $got != "$want" || $linked != "$want" ]]; then
        echo "FAIL $setup: want $want, --check says $got, link says $linked"
        failed=1
    else
        echo "ok   $setup: $want"
    fi
}

expect laptop shown
expect egpu shown
expect desktop hidden
expect desktop-igpu hidden
expect igpu-only hidden
expect two-dgpus hidden
expect empty hidden
exit $failed
