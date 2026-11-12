#!/usr/bin/bash
# SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
#
# SPDX-License-Identifier: GPL-3.0-only

set -euo pipefail

dir=${FAKE_UM_DIR:?}

if [[ $* == "tweaks list --json" ]]; then
    case ${FAKE_UM_LIST:-ok} in
    ok)
        echo '{"version":1,"tweaks":[{"id":"nix","description":"Install Nix","type":"toggle","enabled":false},'
        echo '{"id":"cleanup","description":"Clean up","type":"script","enabled":false}]}'
        ;;
    too-old)
        echo 'flag provided but not defined: -json' >&2
        exit 2
        ;;
    too-new)
        echo '{"version":2,"tweaks":[]}'
        ;;
    unrecognized)
        echo '{"version":"2","tweaks":[]}'
        ;;
    error)
        echo '2026/11/02 13:04:05 open /usr/share/um/tweaks: no such file or directory' >&2
        exit 1
        ;;
    hang)
        sleep 30 &
        echo $! >"$dir/check.pid"
        wait
        ;;
    esac
    exit 0
fi

echo "$*" >"$dir/change.args"
case ${FAKE_UM_CHANGE:-ok} in
ok)
    echo 'TASK [Install Nix] *****'
    echo 'ok: [localhost]'
    ;;
fail)
    echo 'TASK [Install Nix] *****'
    echo '2026/11/02 13:04:05 tweak is locked' >&2
    exit 1
    ;;
slow)
    echo 'TASK [first step] *****'
    sleep 1
    echo 'TASK [second step] *****'
    touch "$dir/change.done"
    ;;
esac
