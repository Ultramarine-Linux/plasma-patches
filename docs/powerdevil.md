# PowerDevil idle hint

the update helper only installs updates while nobody's using the device, and it reads that from logind: a session counts as in use unless it's locked (`LockedHint`) or idle (`IdleHint`). see [updates KCM](updates-backend.md).

problem is, plasma never calls logind's `SetIdleHint` ([KDE bug 448942](https://bugs.kde.org/show_bug.cgi?id=448942)). a plasma session's `IdleHint` just stays false, so the device only counts as unused once the screen locks. turn off screen locking and updates never install automatically.

so this patch backports upstream [powerdevil!699](https://invent.kde.org/plasma/powerdevil/-/merge_requests/699), "Report the idle state of the session with SetIdleHint". it:

- adds a bundled `SessionIdleHint` action that sets the session's `IdleHint` to true after `SessionIdleTimeoutSec` without input, and back to false on activity. this doesn't depend on the dim or screen-off settings
- sends the hint to the session the policy agent already holds, so it works with ConsoleKit as well as logind, and sends it again if logind restarts

## what's left out

the MR's other commit (`7831b2f7`, "policyagent: also inhibit the logind idle action") isn't included. it adds `idle` to the logind block inhibitor powerdevil holds for its whole lifetime.

the problem: powerdevil mirrors every other block inhibitor in logind's `ListInhibitors` into its own inhibitions (`checkLogindInhibitions()`), skipping only its own PID, with no user or session filter, and maps `idle` to "don't change screen settings". with two plasma sessions (fast user switching), each powerdevil would pick up the other one's inhibitor and stop dimming or turning off the screen while its session is unlocked.

without that commit, a logind `IdleAction=` other than the default `ignore` can act on the hint.

## inhibitions

the hint ignores inhibitions. a video player holding a screensaver inhibition still lets `IdleHint` go true after `SessionIdleTimeoutSec` without input, and powerdevil doesn't pass screensaver inhibitions on to logind. so the update helper counts the device as in use while a non-powerdevil logind `idle` block inhibitor is held, or while powerdevil reports a screen inhibition in the user's session (see [updates KCM](updates-backend.md#when-the-device-counts-as-in-use)).

any change in which inhibitions are in effect makes powerdevil simulate user activity (`KIdleTime::simulateUserActivity()`, KDE bug 315438), and any change to the inhibitions restarts its idle timeouts. both can reset the hint to false and restart the `SessionIdleTimeoutSec` count, like when a player starts or stops.

## configuration

it's `SessionIdleTimeoutSec` in the `[General]` group of `powerdevilrc`, in seconds. it defaults to 300, and 0 never reports the session as idle. there's no UI for it.

```sh
kwriteconfig6 --file powerdevilrc --group General --key SessionIdleTimeoutSec 600
```

powerdevil picks it up when it reloads its profile, either through its `refreshStatus` D-Bus method (which the power management KCM calls when saving) or on the next login:

```sh
busctl --user call org.kde.Solid.PowerManagement /org/kde/Solid/PowerManagement \
    org.kde.Solid.PowerManagement refreshStatus
```

check the hint with `loginctl show-session "$XDG_SESSION_ID" -p IdleHint -p IdleSinceHint`.

the update helper waits its own `inactive_minutes` on top of that, so the device counts as unused after `SessionIdleTimeoutSec` plus `inactive_minutes`.

## patch versions

- [`powerdevil/0001-Report-the-idle-state-of-the-session-with-SetIdleHint.patch`](../powerdevil/0001-Report-the-idle-state-of-the-session-with-SetIdleHint.patch): powerdevil 6.8.x, tested with 6.8.2 (fedora 44's `powerdevil-6.8.2-1.fc44`, which has no patches of its own). it applies with `--fuzz=0` to both the release tarball and the `v6.8.2` tag

the patch is the MR's `bc11bfa0` (as of 2026-10-17) with Méven Car's authorship kept. the code is unchanged, and only the commit message says what's left out and why. the MR is still open and review might change it, especially the setting: one reviewer suggested following the profile's `AutoSuspendIdleTimeoutSec` instead of adding a new key. if it changes before it lands, update the patch here.

## packaging

powerdevil is packaged in [Ultramarine-Linux/packages](https://github.com/Ultramarine-Linux/packages) as `ultramarine-powerdevil`, which provides, conflicts with and obsoletes fedora's `powerdevil`. it works just like `ultramarine-plasma-discover` (see [discover patches](discover.md)), and `update.rhai` generates the spec, so don't edit it by hand:

1. grabs fedora's stable `powerdevil` build for the branch's release from bodhi, plus that build's spec, `sources` and patches from dist-git
2. takes the patch from `main` here: `powerdevil/<major.minor>/` if that directory exists, otherwise `powerdevil/`
3. checks the patch applies with `--fuzz=0` on top of fedora's patches, and only then writes the spec with it as `Patch300`. fedora's spec has no "downstream patches" section, so the patch goes right after the `Source` lines. if it doesn't apply, nothing changes until it's rebased here

releases, dist tags and new plasma minor versions work the same as for discover.

CI (`.github/workflows/cpp-ci.yml`) checks weekly (and on changes here) that `powerdevil/` applies with `--fuzz=0` to upstream's `Plasma/6.8` branch.

## dropping the patch

once the MR lands in a plasma release we ship, drop the patch and the package. if it lands with the `idle` inhibitor commit, check first that the cross-session problem above got fixed (like by filtering mirrored inhibitors by session, or skipping powerdevil's own). if upstream renames `SessionIdleTimeoutSec`, make sure anything in ultramarine that sets it moves to the new key.
