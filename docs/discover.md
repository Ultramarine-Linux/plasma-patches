# Discover patches

we got rid of discover's built-in update functionality in favor of our own updates KCM, so discover needed a few patches. they remove the updates page and replace the sidebar entry with a link to our updates page in system settings.

## what the patches do

both versions apply with `--fuzz=0`. they:

- remove the updates mode and swap the sidebar/tab entry for a link to System Settings > Updates
- make `plasma-discover --mode update` (case-insensitive) open System Settings > Updates and show discover's home page. fedora's DiscoverNotifier and scripts use it, and without this the window got stuck on the loading page
- retarget "Configure Updates…"
- make `--headless-update` exit with an error pointing at System Settings > Updates
- drop the "*Distro* N is now available" message and its **Upgrade Now** button from the home page, since it'd start a packagekit system upgrade in discover's hidden updater
- stop the notifier's packagekit plugin from checking for new distro releases, so there's no "Upgrade available" notification either. its **Upgrade** button only opened `--mode update` anyway

discover's rpm-ostree backend (atomic systems) still offers upgrades. the patches don't touch it.

## patch versions

- [`discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): discover 6.8.x, made against 6.8.2 on top of fedora's patches. it also applies to 6.8.1, 6.8.3, `Plasma/6.8` and `master`. this is the one for um45
- [`discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): discover 6.7.0 through 6.7.6, kept around for testing on um44. 6.7.6 changed the sidebar's updates entry, so instead of deleting its update count and fetching icon like the 6.8 patch does, this one sets the entry's `stateObject` to `null`. that leaves the 6.7.6 lines alone, so one patch fits every 6.7 release

## packaging

the spec matches fedora's. it only adds our patch and renames the package to `ultramarine-plasma-discover` instead of upstream's `plasma-discover` (it provides, conflicts with and obsoletes `plasma-discover` and its subpackages). it lives in [Ultramarine-Linux/packages](https://github.com/Ultramarine-Linux/packages), and `update.rhai` generates the spec, so don't edit it by hand. here's what it does:

1. grabs fedora's stable `plasma-discover` build for the branch's release from bodhi, plus that build's spec, `sources` and patches from dist-git
2. takes the patch from `main` here: `discover/<major.minor>/` if that directory exists, otherwise `discover/`
3. checks the patch applies with `--fuzz=0` on top of fedora's patches, and only then writes the spec with it as `Patch300`. if it doesn't apply, nothing changes until it's rebased here

the release follows fedora's and goes up by one when only the patch changes. our `.umNN` dist tag sorts above fedora's `.fcNN`, so ties go our way.

a patch change reaches the package once it's on `main` here. if a new plasma minor version breaks the current patch, add a `discover/<major.minor>/` directory or rebase `discover/`.

### autotests

the patches don't touch discover's QML autotests. `discover/autotests/toplevels.qml` (`test_navigateThenUpdate`) and `updateandinstall.qml` still expect the updates page, so they'd fail with the patch applied. they're only built with `-DBUILD_DummyBackend=ON` though, which is off by default, and neither fedora's spec nor ours sets it or runs `ctest` in `%check`. if the package ever starts running them, fix those tests in the patches.

## CI

`.github/workflows/cpp-ci.yml` checks weekly (and on changes here) that `discover/` applies with `--fuzz=0` to upstream's `Plasma/6.8` and `master`, and `discover/6.7/` to `Plasma/6.7`. fedora's patches aren't applied there, so it doesn't replace `update.rhai`'s check.

## testing

i tested it in a fedora container: `--listmodes` doesn't include Update anymore, and `--headless-update` exits with an error. clicking the system settings link and `plasma-discover --mode update` still need testing in a real plasma session. system settings should open on Updates and discover on Home.
