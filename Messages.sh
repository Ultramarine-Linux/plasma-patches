#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

# Extract KI18n strings to one POT per KCM. Weblate writes translations to
# po/<locale>/<domain>.po; KI18N_INSTALL(po) compiles and installs those files.
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p po
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# shellcheck disable=SC2054
keywords=(
    --keyword=i18n:1
    --keyword=i18nc:1c,2
    --keyword=i18np:1,2
    --keyword=i18ncp:1c,2,3
)

extract_domain() {
    local domain="$1"
    # Space-separated C++ sources.
    read -r -a cpp_sources <<<"$2"
    shift 2
    local cpp_pot="$workdir/${domain}-cpp.pot"
    local qml_pot="$workdir/${domain}-qml.pot"

    xgettext --language=C++ --from-code=UTF-8 --package-name="$domain" \
        "${keywords[@]}" --output="$cpp_pot" "${cpp_sources[@]}"
    xgettext --language=Java --from-code=UTF-8 --package-name="$domain" \
        "${keywords[@]}" --output="$qml_pot" "$@"
    msgcat --use-first --sort-output "$cpp_pot" "$qml_pot" \
        --output="po/${domain}.pot"
}

# The update helper's notifications share the Updates KCM's catalog.
extract_domain kcm_ultramarine_updates \
    "kcm-updates/updates.cpp updater/schedule.cpp" \
    kcm-updates/ui/main.qml \
    kcm-updates/ui/AdvancedSettings.qml \
    kcm-updates/ui/TechnicalInformation.qml

extract_domain kcm_ultramarine_tweaks \
    kcm-tweaks/tweaks.cpp \
    kcm-tweaks/ui/main.qml
