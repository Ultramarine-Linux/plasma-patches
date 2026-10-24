# umcli backend (Tweaks)

The Tweaks KCM is a frontend for `umcli`; it discovers available tweaks from the
CLI rather than maintaining its own list. Updates are handled by
[`ultramarine-updates`](updates-backend.md), not umcli.

## Runtime requirements

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

On page load, it runs `um tweaks list --json`. Each directory in umcli's tweak
data becomes a row, so new tweaks with known types (`toggle`, `oneshot`,
`script`) appear without KCM changes. Unknown types are displayed safely without
an unsupported control.

Enabling, disabling or running a tweak invokes `um tweaks` through umcli's
polkit-aware privilege escalation. Warnings are shown before risky or
non-reversible operations. State is reported by umcli's world-readable markers
under `/var/lib/um/tweaks/`.

For the command behavior, see the [umcli README](https://github.com/Ultramarine-Linux/um/blob/main/README.md).
