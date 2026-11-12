#!/usr/bin/bash
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

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

card() {
    local setup=$1 name=$2 boot_vga_or_dash=$3 discrete=$4
    local dir=$tmp/$setup/$name connector=$tmp/$setup/$name-eDP-1
    mkdir -p "$dir/device" "$connector"
    ln -s "../$name/device" "$connector/device"
    if [[ $boot_vga_or_dash != - ]]; then echo "$boot_vga_or_dash" >"$dir/device/boot_vga"; fi
    if [[ $discrete == 1 ]]; then
        echo "TAGS=:seat:switcheroo-discrete-gpu:" >"$dir/fake-udev"
    else
        echo "TAGS=:seat:" >"$dir/fake-udev"
    fi
}

card laptop card0 1 0
card laptop card1 0 1
card laptop-3d card0 1 0
card laptop-3d card1 - 1
card egpu card0 1 0
card egpu card2 0 1
card desktop card0 1 1
card desktop-igpu card0 0 0
card desktop-igpu card1 1 1
card igpu-only card0 1 0
card two-dgpus card0 1 1
card two-dgpus card1 0 1
# grep -q stops reading at the match, and udevadm's SIGPIPE must not count as not discrete
card noisy card0 1 0
card noisy card1 0 1
for i in $(seq 200000); do echo "PADDING_$i=x"; done >>"$tmp/noisy/card1/fake-udev"
card simpledrm-laptop card0 - 0
card simpledrm-laptop card1 1 0
card simpledrm-laptop card2 0 1
card simpledrm-desktop card0 - 0
card simpledrm-desktop card1 1 1
card simpledrm-only card0 - 0
card no-boot-vga card0 - 0
card no-boot-vga card1 - 1
mkdir -p "$tmp/empty"

failed=0
expect() {
    local setup=$1 want=$2 link=$tmp/run/$1/action.desktop
    export PATH=$tmp/bin:$PATH ULTRAMARINE_DGPU_DRM_DIR=$tmp/$setup ULTRAMARINE_DGPU_LINK=$link

    local got=hidden
    if "$detect" --check >/dev/null; then got=shown; fi

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
expect laptop-3d shown
expect egpu shown
expect desktop hidden
expect desktop-igpu hidden
expect igpu-only hidden
expect two-dgpus hidden
expect noisy shown
expect simpledrm-laptop shown
expect simpledrm-desktop hidden
expect simpledrm-only hidden
expect no-boot-vga hidden
expect empty hidden
exit $failed
