# umcli backend (Tweaks)

The Tweaks KCM is a frontend for `umcli` and gets the list of tweaks from it instead of
keeping its own. Updates are handled by [`ultramarine-updates`](updates-backend.md).

## Runtime requirements

- umcli with [um#26](https://github.com/Ultramarine-Linux/um/pull/26), which adds
  `um tweaks list --json` (with `state_error`), the polkit action and pkexec escalation
  without a terminal. It isn't merged or released yet, so no released umcli works with this
  KCM. Older um releases get a "too old" message instead of a list.
- Kirigami and KCMUtils' QML modules. Unlike [Location Services](location.md), this page
  doesn't use `kirigami-addons`.
- `umcli` must install `/usr/bin/um` and `/usr/share/um`. The KCM prefers
  `/usr/bin/um`. CMake's `-DUM_EXECUTABLE=...` setting can point development builds
  elsewhere. If that path doesn't exist, the KCM falls back to the first `um` in `PATH`,
  which still works, but polkit then uses its generic `org.freedesktop.policykit.exec`
  action (generic prompt, no remembered authorization) instead of umcli's.
- The `umcli` package must install `polkit/org.ultramarine.um.policy` at
  `/usr/share/polkit-1/actions/org.ultramarine.um.policy`. The KCM runs `um` as the
  logged-in user, and umcli re-executes through `pkexec` when it needs root.
- `pkexec` and a running polkit authentication agent. `pkexec` clears the environment, so um refuses to escalate through it while
  `UM_TWEAKS_PATH`, `UM_DATA` or `UM_STATE` is set rather than silently using the
  installed `/usr/share/um` data. The KCM shows that error.

## Tweaks KCM

Whenever the page is shown, it runs `um tweaks list --json`. Each directory in umcli's tweak
data becomes a row, so new tweaks with known types (`toggle`, `oneshot`,
`script`) appear without KCM changes. Unknown types are listed without a control.

Enabling, disabling or running a tweak runs `um tweaks`, which escalates through
polkit. Risky or irreversible tweaks show a warning first. um is started without stdin and in a new session, so it
never sees a terminal and escalates through pkexec (with Qt 6.6, which has no
`CreateNewSession`, the KCM calls `setsid()` in the child itself). The exception is a
`wheel` member for whom sudo needs no password (a `NOPASSWD` rule or cached credentials): um then uses
`sudo -n`, which forwards the `UM_*` overrides and exits 1 rather than 126 or 127 if it
fails.

Whether a tweak is on comes from umcli: a tweak's `check.sh` decides when it has
one, and the world-readable markers under `/var/lib/um/tweaks/` are the fallback
when it doesn't (or when it exits with "unknown"). When the check fails, um sets
`state_error`, and the KCM shows the state as unknown with the error and doesn't
let the tweak be changed from there.

Since um leads its own session and process group, closing the page while the list is
loading kills the whole group, so the `check.sh` scripts um runs to read the state don't
outlive it. A running change can't be canceled from the KCM: once um runs as root, the
unprivileged KCM can't signal it. Closing the page or System Settings leaves it
running rather than killing it halfway. Its output goes to a `0600` file in
`$XDG_RUNTIME_DIR` (unlinked as soon as um starts, and polled by the KCM) instead of a
pipe, because Go kills a program that writes to a closed pipe on stdout or stderr. um is
also started with SIGPIPE ignored.

um exits with pkexec's 126 or 127 when authentication was canceled or failed. It
maps a playbook's or script's own 126 and 127 to 1, so those codes always mean nothing
changed.

`um tweaks list --json` output has a `version`. The KCM only accepts version 1 (or no
version, from um builds before it was added) and asks for a newer System Settings
otherwise, since a new version may change what the fields mean. A `version` that isn't a
positive whole number (a string, a fraction, `null`, 0 or less) isn't read as 1 either, and
gets its own error.

For the command behavior, see the [umcli README](https://github.com/Ultramarine-Linux/um/blob/main/README.md).
