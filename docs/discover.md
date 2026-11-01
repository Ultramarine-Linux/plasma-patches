# Discover integration

Ultramarine's Updates KCM (and its `ultramarine-updates` helper) handles updates. The Discover patch removes
Discover's own Updates page and directs users to
`kcm_ultramarine_updates` instead.

## Patch versions

- [`../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): Discover 6.8.x (tested with 6.8.1); intended for UM45.
- [`../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch`](../discover/6.7/0001-Remove-Discover-updates-UI-in-favor-of-um.patch): Discover 6.7.5, kept for testing against the installed UM44-era build.

Both patches apply with `--fuzz=0` (the 6.8 one also applies to 6.8.2). They:

- remove the Updates mode and replace the sidebar/tab entry with a link to
  System Settings > Updates;
- make `plasma-discover --mode update` (case-insensitive) open System Settings >
  Updates and show Discover's Home page. Fedora's DiscoverNotifier and scripts use
  it; without this the window stayed on the loading page;
- retarget "Configure Updates…";
- make `--headless-update` exit with an error directing users to System Settings > Updates;
- drop the "*Distro* N is now available" message with **Upgrade Now** from the
  Home page. It would start a PackageKit system upgrade in Discover's hidden updater.

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

So a patch change reaches the package once it's on `main` here. A new Plasma minor
version that the current patch doesn't apply to needs a `discover/<major.minor>/`
directory (or a rebase of `discover/`).

The patch was also runtime-tested in a Fedora container: Discover's `--listmodes`
no longer includes Update, and `--headless-update` exits with an error. Clicking
the System Settings link and `plasma-discover --mode update` still need testing in a
real Plasma session: System Settings should open on Updates and Discover on Home.
