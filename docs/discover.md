# Discover integration

Ultramarine's Updates KCM (and its `ultramarine-updates` helper) handles updates. The Discover patch removes
Discover's own Updates page and directs users to
`kcm_ultramarine_updates` instead.

## Patch versions

- [`../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): Discover 6.8.x (tested with 6.8.1); intended for UM45.
- [`../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): Discover 6.7.5, kept for testing against the installed UM44-era build.

The 6.8.1 patch removes the Updates mode, replaces the sidebar/tab entry with a
link to System Settings, retargets "Configure Updates…", and makes
`--headless-update` exit with an error directing users to System Settings > Updates.

## Packaging

The spec in [`../discover/plasma-discover.spec`](../discover/plasma-discover.spec)
builds the Discover app and its supported backend subpackages from the same
source. This matters because the patch changes Discover's application and QML;
the existing Ultramarine spec only shipped the PackageKit backend while Fedora
provided the app.

For UM45, the spec:

- applies the 6.8.1 patch;
- builds main, `-libs`, `-packagekit`, `-flatpak` and `-kns` packages;
- disables Discover's own updates KCM and notifier, and removes leftover
  notifier plugins/configuration;
- obsoletes the Fedora notifier, offline-updates, rpm-ostree and snap subpackages.

The KDE bug 512957 revert is not carried: the issue is fixed upstream in 6.8.1.
The old PackageKit notifier refresh patch and `discoverrc` are no longer used.

The spec was built and install-tested with Anda's `rpmbuild` backend in a Fedora
rawhide container. There is still a packaging TODO to recommend
`ultramarine-plasma-settings` once that KCM package is added to the repository.

The patch was also runtime-tested in a Fedora container: Discover's `--listmodes`
no longer includes Update, and `--headless-update` exits with an error. Clicking
the System Settings link still needs testing in a real Plasma session.
