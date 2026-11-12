#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

cd "$(dirname "$0")"
domain=ultramarine-plasma-settings
pot="po/${domain}.pot"
polkit_its="${POLKIT_ITS:-/usr/share/gettext/its/polkit.its}"

[ -f "$polkit_its" ] || {
    echo "polkit ITS rules not found at $polkit_its (install polkit-devel or set POLKIT_ITS)" >&2
    exit 1
}

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

files() {
    git ls-files -- "$@" ':!:tests/**' | sort
}
mapfile -t cpp_sources < <(files '*.cpp' '*.h')
mapfile -t qml_sources < <(files '*.qml')
mapfile -t desktop_sources < <(files '*.desktop.in')
mapfile -t policy_sources < <(files '*.policy.in')
mapfile -t json_sources < <(files '*.json.in')

# shellcheck disable=SC2054
keywords=(
    --keyword=i18n:1
    --keyword=i18nc:1c,2
    --keyword=i18np:1,2
    --keyword=i18ncp:1c,2,3
)
# file-only locations so moving code doesn't churn the template
common=(--from-code=UTF-8 --add-comments=i18n: --add-location=file)

# --kde only works for C++, and QML parses well enough as C++ like in KDE's own extraction
xgettext --language=C++ --kde "${common[@]}" "${keywords[@]}" \
    --output="$workdir/cpp.pot" "${cpp_sources[@]}"
xgettext --language=C++ --kde "${common[@]}" "${keywords[@]}" \
    --output="$workdir/qml.pot" "${qml_sources[@]}"
xgettext --language=Desktop "${common[@]}" \
    --output="$workdir/desktop.pot" "${desktop_sources[@]}"
xgettext --its="$polkit_its" "${common[@]}" \
    --output="$workdir/policy.pot" "${policy_sources[@]}"
python3 -I cmake/kcm-json-l10n.py extract "$workdir/json.pot" "${json_sources[@]}"

# msgcat drops the header when the first catalog is empty, so put a header first.
# xgettext dates it from its input's mtime, and /dev/null's mtime is the last boot
: >"$workdir/empty.c"
xgettext --language=C --force-po --package-name="$domain" \
    --msgid-bugs-address=https://github.com/Ultramarine-Linux/plasma-patches/issues \
    --output="$workdir/header.pot" "$workdir/empty.c"
sed -i 's/charset=CHARSET/charset=UTF-8/' "$workdir/header.pot"

msgcat --use-first --sort-output --add-location=file \
    "$workdir/header.pot" "$workdir"/{cpp,qml,desktop,policy,json}.pot \
    --output="$workdir/new.pot"

# keep the old POT-Creation-Date so CI's freshness check only fails when strings change
mkdir -p po
if [ -f "$pot" ] && diff -q -I '^"POT-Creation-Date:' "$pot" "$workdir/new.pot" >/dev/null; then
    echo "$pot is up to date"
else
    mv "$workdir/new.pot" "$pot"
    echo "wrote $pot"
fi
