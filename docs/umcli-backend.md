# umcli backend

Both KCMs are frontends for `umcli`; they discover available data from the CLI
rather than maintaining their own package or tweak lists.

## Runtime requirements

- `umcli` must install `/usr/bin/um` and `/usr/share/um`. The KCMs prefer
  `/usr/bin/um`; CMake's `-DUM_EXECUTABLE=...` setting can point development builds
  elsewhere.
- The `umcli` package must install
  `polkit/org.ultramarine.um.policy` at
  `/usr/share/polkit-1/actions/org.ultramarine.um.policy`. The KCMs run `um` as
  the logged-in user; when it needs root, umcli re-executes through `pkexec`.
  This preserves user Flatpak and Nix profile access before system updates run.
- `pkexec` and an active polkit authentication agent are needed for privileged
  actions. `pkexec` clears environment overrides, so privileged tweak actions
  use the installed `/usr/share/um` data.

## Updates KCM

On page load, it runs `um upgrade list --json`. The response provides apps,
packages and runtimes, including names, icons, sources, versions, download sizes
and installed-size changes where available. Apps are shown first; the page shows
the first five and links to a full Technical Information sub-page.

- **Apply** runs `um upgrade apply --yes`.
- **Apply Tonight** calls `um upgrade schedule`; the scheduled update waits for
  the configured idle time and optional time window.
- **Automatic Updates** and **Advanced Settings** read/save the system-wide
  `um upgrade settings --json` configuration. System Settings' Apply and Reset
  buttons manage the unsaved changes.

## Tweaks KCM

On page load, it runs `um tweaks list --json`. Each directory in umcli's tweak
data becomes a row, so new tweaks with known types (`toggle`, `oneshot`,
`script`) appear without KCM changes. Unknown types are displayed safely without
an unsupported control.

Enabling, disabling or running a tweak invokes `um tweaks` through umcli's
polkit-aware privilege escalation. Warnings are shown before risky or
non-reversible operations. State is reported by umcli's world-readable markers
under `/var/lib/um/tweaks/`.

For the command behavior and systemd timers, see the [umcli README](https://github.com/Ultramarine-Linux/um/blob/main/README.md).
