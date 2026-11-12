# PowerDevil idle hint

Ultramarine's update helper only installs updates while nobody is using the
device. It reads that from logind: a session counts as in use unless it's
locked (`LockedHint`) or idle (`IdleHint`). See [update helper](updates-backend.md).
Plasma never calls logind's `SetIdleHint` ([KDE bug 448942](https://bugs.kde.org/show_bug.cgi?id=448942)),
so a Plasma session's `IdleHint` stays false, and the device only counts as
unused once the screen is locked. If screen locking is turned off, updates never
install automatically.

The PowerDevil patch backports upstream
[powerdevil!699](https://invent.kde.org/plasma/powerdevil/-/merge_requests/699),
"Report the idle state of the session with SetIdleHint". It:

- adds a bundled `SessionIdleHint` action that sets the session's `IdleHint` to
  true after `SessionIdleTimeoutSec` without input and back to false on activity.
  This doesn't depend on the dim or screen-off settings;
- sends the hint to the session the policy agent already holds, so it works with
  ConsoleKit as well as logind, and sends it again if logind restarts.

The MR's other commit (`7831b2f7`, "policyagent: also inhibit the logind idle
action") is left out. It adds `idle` to the logind block inhibitor
PowerDevil holds for its whole lifetime. PowerDevil also mirrors every other
block inhibitor in logind's `ListInhibitors` into its own inhibitions
(`checkLogindInhibitions()`), skipping only its own PID, with no user or session
filter, and maps `idle` to "don't change screen settings". With two Plasma
sessions (fast user switching), each PowerDevil would pick up the other one's
lock and stop dimming or turning off the screen while its session is unlocked.
Without that commit, a logind `IdleAction=` other than the default `ignore` can
act on the hint.

## Inhibitions

The hint ignores inhibitions. A video player holding a screensaver inhibition
still lets `IdleHint` go true after `SessionIdleTimeoutSec` without input, and
PowerDevil doesn't pass screensaver inhibitions on to logind. So the update helper
counts the device as in use while a non-PowerDevil logind `idle` block inhibitor
is held, or while PowerDevil reports a screen inhibition in the user's session
(see [update helper](updates-backend.md)).

Any change in which inhibitions are in effect makes PowerDevil simulate user
activity (`KIdleTime::simulateUserActivity()`, KDE bug 315438), and any change
to the inhibitions restarts its idle timeouts. Both can reset the hint to false
and start the `SessionIdleTimeoutSec` count over, e.g. when a player starts or
stops.

## Configuration

`SessionIdleTimeoutSec` in the `[General]` group of `powerdevilrc`, in seconds.
It defaults to 300, and 0 never reports the session as idle. There's no UI.

```sh
kwriteconfig6 --file powerdevilrc --group General --key SessionIdleTimeoutSec 600
```

PowerDevil picks it up when it reloads its profile: on its `refreshStatus` D-Bus
method (which the Power Management KCM calls when saving), or on the next login.

```sh
busctl --user call org.kde.Solid.PowerManagement /org/kde/Solid/PowerManagement \
    org.kde.Solid.PowerManagement refreshStatus
```

Check the hint with
`loginctl show-session "$XDG_SESSION_ID" -p IdleHint -p IdleSinceHint`.

The update helper waits its own `inactive_minutes` on top of that, so the device
counts as unused after `SessionIdleTimeoutSec` plus `inactive_minutes`.

## Patch versions

- [`../powerdevil/0001-Report-the-idle-state-of-the-session-with-SetIdleHint.patch`](../powerdevil/0001-Report-the-idle-state-of-the-session-with-SetIdleHint.patch):
  PowerDevil 6.8.x, tested with 6.8.2 (Fedora 44's `powerdevil-6.8.2-1.fc44`,
  which has no patches of its own). It applies with `--fuzz=0` to both the
  release tarball and the `v6.8.2` tag.

The patch is the MR's `bc11bfa0` (as of 2026-10-17) with Méven Car's authorship
kept. Its code is unchanged, and only the commit message says what's left out
and why. The MR is still open, and review may change it, in particular the
setting: one reviewer suggested following the profile's
`AutoSuspendIdleTimeoutSec` instead of a new key. Update the patch here if it
changes before it lands.

## Packaging

PowerDevil is packaged in [Ultramarine-Linux/packages](https://github.com/Ultramarine-Linux/packages)
as `ultramarine-powerdevil`, which provides, conflicts with and obsoletes Fedora's
`powerdevil`. It works like `ultramarine-plasma-discover` (see
[Discover integration](discover.md)). Its `update.rhai` generates the spec, so
don't edit it by hand:

1. It takes Fedora's stable `powerdevil` build for the branch's release from Bodhi,
   and that build's spec, `sources` and patches from dist-git.
2. It takes the patch from `main` here: `powerdevil/<major.minor>/` if that
   directory exists, otherwise `powerdevil/` (the current Plasma release).
3. It checks the patch applies with `--fuzz=0` on top of Fedora's patches, and only
   then writes the spec with the patch as `Patch300`. Fedora's spec has no
   "downstream patches" section, so the patch goes right after the `Source` lines.
   If it doesn't apply, nothing changes until it's rebased here.

Releases, dist tags and new Plasma minor versions work the same as for Discover: a
new minor version the patch doesn't apply to needs a `powerdevil/<major.minor>/`
directory or a rebase of `powerdevil/`.

CI (`.github/workflows/cpp-ci.yml`) checks weekly, and on changes here, that
`powerdevil/` applies with `--fuzz=0` to upstream's `Plasma/6.8` branch.

## Dropping the patch

Drop the patch, and the package, once the MR lands in a Plasma release that
Ultramarine ships. If it lands with the `idle` inhibitor commit, check first that
the cross-session problem above was fixed (e.g. by filtering mirrored inhibitors
by session, or skipping PowerDevil's own). If upstream renames `SessionIdleTimeoutSec`, check that
anything Ultramarine sets it in moves to the new key.
