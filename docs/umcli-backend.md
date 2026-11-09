# umcli backend (Tweaks)

The Tweaks KCM is a frontend for `umcli`; it discovers available tweaks from the
CLI rather than maintaining its own list. Updates are handled by
[`ultramarine-updates`](updates-backend.md), not umcli.

## Runtime requirements

- umcli with [um#26](https://github.com/Ultramarine-Linux/um/pull/26), which adds
  `um tweaks list --json` (with `state_error`), the polkit action and pkexec escalation
  without a terminal. It isn't merged or released yet, so no released umcli works with this
  KCM. Older um releases get a "too old" message instead of a list.
- Kirigami and KCMUtils' QML modules. Unlike [Location Services](location.md), this page
  doesn't use `kirigami-addons`.
- `umcli` must install `/usr/bin/um` and `/usr/share/um`. The KCM prefers
  `/usr/bin/um`; CMake's `-DUM_EXECUTABLE=...` setting can point development builds
  elsewhere.
- The `umcli` package must install
  `polkit/org.ultramarine.um.policy` at
  `/usr/share/polkit-1/actions/org.ultramarine.um.policy`. The KCM runs `um` as
  the logged-in user; when it needs root, umcli re-executes through `pkexec`.
- `pkexec` and an active polkit authentication agent are needed for privileged
  actions. `pkexec` clears environment overrides, so privileged tweak actions
  use the installed `/usr/share/um` data.

## Tweaks KCM

Whenever the page is shown, it runs `um tweaks list --json`. Each directory in umcli's tweak
data becomes a row, so new tweaks with known types (`toggle`, `oneshot`,
`script`) appear without KCM changes. Unknown types are displayed safely without
an unsupported control.

Enabling, disabling or running a tweak invokes `um tweaks` through umcli's
polkit-aware privilege escalation. Warnings are shown before risky or
non-reversible operations. um is started without stdin and in a new session, so it
never sees a terminal and always escalates through pkexec rather than sudo.

Whether a tweak is on comes from umcli: a tweak's `check.sh` decides when it has
one, and the world-readable markers under `/var/lib/um/tweaks/` are the fallback
when it doesn't (or when it exits with "unknown"). When the check fails, um sets
`state_error`, and the KCM shows the state as unknown with the error and doesn't
let the tweak be changed from there.

A running change can't be canceled from the KCM: once um runs as root, the
unprivileged KCM can't signal it. Closing the page or System Settings leaves it
running rather than killing it halfway. Its output goes to a `0600` file in
`$XDG_RUNTIME_DIR` (unlinked as soon as um starts, and polled by the KCM) instead of a
pipe, because Go kills a program that writes to a closed pipe on stdout or stderr. um is
also started with SIGPIPE ignored.

um exits with pkexec's 126 or 127 when authentication was canceled or failed.
It maps a failed playbook's 126 and 127 to 1, so for toggles and one-time changes
those codes mean nothing changed. `um tweaks run` execs the tweak's `script.sh`
directly though, so a script exiting with 126 or 127 looks the same, and the KCM
doesn't claim nothing changed for those.

For the command behavior, see the [umcli README](https://github.com/Ultramarine-Linux/um/blob/main/README.md).
