#!/usr/bin/bash
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# runs ultramarine-dgpu-detect against fake /sys/class/drm trees, using a
# fake udevadm that reports the switcheroo-discrete-gpu tag
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

# card <setup> <name> <boot_vga, or - for none> <discrete>
card() {
    local dir=$tmp/$1/$2
    mkdir -p "$dir/device" "$tmp/$1/$2-eDP-1"
    # real connectors link to their card's device too
    ln -s "../$2/device" "$tmp/$1/$2-eDP-1/device"
    # 3D controllers (PCI class 0x0302), common for laptop dGPUs, have no boot_vga
    if [[ $3 != - ]]; then echo "$3" >"$dir/device/boot_vga"; fi
    if [[ $4 == 1 ]]; then
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
# udevadm keeps writing after the tag. grep -q stops reading at the match, which
# must not make udevadm's SIGPIPE count as "not discrete"
card noisy card0 1 0
card noisy card1 0 1
for i in $(seq 200000); do echo "PADDING_$i=x"; done >>"$tmp/noisy/card1/fake-udev"
# simpledrm (or another platform device) has no boot_vga and isn't discrete, and is
# often card0 while the real drivers load
card simpledrm-laptop card0 - 0
card simpledrm-laptop card1 1 0
card simpledrm-laptop card2 0 1
card simpledrm-desktop card0 - 0
card simpledrm-desktop card1 1 1
card simpledrm-only card0 - 0
# without any boot_vga there's no default GPU to compare against
card no-boot-vga card0 - 0
card no-boot-vga card1 - 1
mkdir -p "$tmp/empty"

failed=0
expect() {
    local setup=$1 want=$2 link=$tmp/run/$1/action.desktop
    export PATH=$tmp/bin:$PATH ULTRAMARINE_DGPU_DRM_DIR=$tmp/$setup ULTRAMARINE_DGPU_LINK=$link

    local got=hidden
    if "$detect" --check >/dev/null; then got=shown; fi

    # without --check it makes or removes the link. Start from a stale link.
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
