# Discover integration

The Discover patch removes Discover's own Updates page and sends users to the
Ultramarine Updates KCM (`kcm_ultramarine_updates`) instead.

## Patch versions

- [`../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): Discover 6.8.x, made against 6.8.2 (on top of Fedora's
  patches); it also applies to 6.8.1, 6.8.3 and the `Plasma/6.8` and `master` branches. Intended for UM45.
- [`../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): Discover 6.7.x (6.7.0 through 6.7.6),
  kept for testing against the installed UM44-era build. 6.7.6 changed the sidebar's
  Updates entry, so instead of deleting its update count and fetching icon like the 6.8
  patch does, this one sets the entry's `stateObject` to `null`. That leaves the 6.7.6
  lines alone, so one patch fits every 6.7 release.

Both patches apply with `--fuzz=0`. They:

- remove the Updates mode and replace the sidebar/tab entry with a link to
  System Settings > Updates;
- make `plasma-discover --mode update` (case-insensitive) open System Settings >
  Updates and show Discover's Home page (Fedora's DiscoverNotifier and scripts use
  it, and without this the window stayed on the loading page);
- retarget "Configure Updates…";
- make `--headless-update` exit with an error directing users to System Settings > Updates;
- drop the "*Distro* N is now available" message with **Upgrade Now** from the
  Home page. It would start a PackageKit system upgrade in Discover's hidden updater;
- stop the notifier's PackageKit plugin from checking for a new distro release, so
  there's no "Upgrade available" notification either. Its **Upgrade** button only
  opened `--mode update`. Discover's rpm-ostree backend (atomic systems) still offers
  upgrades, which this patch doesn't touch.

## Packaging

Discover is packaged in [Ultramarine-Linux/packages](https://github.com/Ultramarine-Linux/packages)
as `ultramarine-plasma-discover`, which provides, conflicts with and obsoletes
Fedora's `plasma-discover` and its subpackages. Its `update.rhai` generates the spec;
don't edit it by hand:

1. It takes Fedora's stable `plasma-discover` build for the branch's release from
   Bodhi, and that build's spec, `sources` and patches from dist-git.
2. It takes the patch from `main` here: `discover/<major.minor>/` if that directory
   exists, otherwise `discover/` (the current Plasma release).
3. It checks the patch applies with `--fuzz=0` on top of Fedora's patches, and only
   then writes the spec with the patch as `Patch300`. If it doesn't apply, nothing
   changes until it's rebased here.

The release follows Fedora's, and goes up by one when only the patch changes.
Ultramarine's `.umNN` dist tag sorts above Fedora's `.fcNN`, so ties go our way.

A patch change reaches the package once it's on `main` here. A new Plasma minor
version that the current patch doesn't apply to needs a `discover/<major.minor>/`
directory or a rebase of `discover/`.

Neither patch touches Discover's QML autotests. `discover/autotests/toplevels.qml`
(`test_navigateThenUpdate`) and `updateandinstall.qml` still expect the Updates page,
so they'd fail with the patch applied. They're only built with `-DBUILD_DummyBackend=ON`
(off by default), and neither Fedora's `plasma-discover` spec nor
`ultramarine-plasma-discover` sets it or runs `ctest` in `%check` (only
`appstream-util` and `desktop-file-validate`), so the package build isn't affected.
Adjust those tests in the patches if the package ever starts running them.

CI (`.github/workflows/cpp-ci.yml`) checks weekly, and on changes here, that
`discover/` applies with `--fuzz=0` to upstream's `Plasma/6.8` and `master`, and
`discover/6.7/` to `Plasma/6.7`. Fedora's own patches aren't applied there, so it
doesn't replace `update.rhai`'s check.

The patch was also runtime-tested in a Fedora container: Discover's `--listmodes`
no longer includes Update, and `--headless-update` exits with an error. Clicking
the System Settings link and `plasma-discover --mode update` still need testing in a
real Plasma session: System Settings should open on Updates and Discover on Home.
