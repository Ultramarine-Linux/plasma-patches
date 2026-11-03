# Office suite installer

Ultramarine doesn't install an office suite by default. When no suite is installed,
opening an office document (ODF, OOXML, legacy Microsoft Office, iWork, WordPerfect)
shows a dialog that offers to install one:

- **Collabora Office** (recommended): Flathub, or Terra's RPM (`collabora-office`).
- **LibreOffice**: Flathub, or Fedora's RPMs (`libreoffice-writer`, `-calc`, `-impress`).
- **ONLYOFFICE**: Flathub only.

Each suite shows its Flathub icon, from the AppStream data Flatpak keeps in
`/var/lib/flatpak/appstream` (or the user installation's), so nothing is downloaded
to show the dialog. Without AppStream data, it falls back to a document icon.

Flathub is always the default. RPMs aren't offered on image based (bootc or
rpm-ostree) systems, where `/run/ostree-booted` exists.

## How it works

- **The handler:** [`../office-installer/data/org.ultramarine.OfficeInstaller.desktop.in`](../office-installer/data/org.ultramarine.OfficeInstaller.desktop.in)
  lists the office MIME types and runs `/usr/libexec/ultramarine-office-installer %U`.
  It's `NoDisplay`, so it isn't in the app launchers. File managers' **Open With** menus
  list every handler, `NoDisplay` or not, so it shows up there as **Office Suite
  Installer** even with a suite installed. Formats that are also plain text (CSV, RTF,
  flat ODF) are left out, so they still open in a text editor. The desktop file's `Name`
  and `Comment` aren't in the gettext catalogs: `Messages.sh` only extracts C++ strings.
- **Once a suite is installed:** KF6 ignores `InitialPreference`. Handlers that aren't
  in a `mimeapps.list` all have the same preference, and KService sorts them by desktop
  file name, so `org.ultramarine.OfficeInstaller` comes after `com.collaboraoffice.*`,
  `libreoffice-*`, `org.libreoffice.*` and `org.onlyoffice.*`. Fedora's `mimeapps.list`
  also makes `libreoffice-*.desktop` the default. When the installer still gets picked
  (a suite that sorts later, like WPS, or GIO, which doesn't rank handlers), it looks
  for another app that lists the document's MIME type itself. If it finds one, it opens
  the document there without showing anything. Parent types don't count: a `.docx` is a
  zip, but Ark isn't an office suite.
- **A suite that can't open the file:** ONLYOFFICE doesn't open every format the
  installer claims (Microsoft Works, WordPerfect, ODF charts, master documents and
  HTML templates). If something opens DOCX, ODT, XLSX, ODS, PPTX or ODP, a suite is
  installed, so the dialog says that suite can't open the file and offers the
  other suites instead of saying none is installed.
- **Installing:** Flatpaks install from the `flathub` system remote with libflatpak
  (`app/<id>/<arch>/stable`, like `flatpak install --system flathub <id>`), and RPMs through
  PackageKit's D-Bus API (`Resolve`, then `InstallPackages` with only trusted packages).
  Both authorize through polkit, which asks non-admin users for an admin password in the
  session. The command line tools can't do that from a GUI: `flatpak install -y` and
  `--noninteractive`, `pkcon -y` and `pkgcli -y` all mark the transaction non-interactive,
  so polkit refuses with "Not authorized" instead of asking. Without `-y` they ask their
  own questions on a terminal (flatpak answers "no" when stdin isn't one). Wheel users
  install Flatpaks without a prompt (Fedora's `org.freedesktop.Flatpak.rules`).
- **Canceling:** **Cancel** stays available while installing. It cancels the libflatpak
  transaction or the PackageKit transaction (PackageKit's `Cancel`), and the dialog goes
  back to the suite list. Flatpak deploys an app only once it's fully downloaded, so a
  canceled install leaves at most downloaded data behind. PackageKit refuses to cancel
  once it's installing the RPMs, so then the install finishes and the documents open.
- **Afterwards:** the installer refreshes KSycoca and opens the documents with the new
  suite. If KSycoca doesn't know about the suite yet, it runs the suite directly
  (`flatpak run --file-forwarding`, `coda-qt` or `libreoffice`). If an app fails to
  start, the error is shown instead of quitting silently.
- **One at a time:** the dialog exports its `addDocuments` method, then takes
  `org.ultramarine.OfficeInstaller` on the session bus, so a launch that sees the name can
  always reach it. Opening more documents while it's open hands them to it, so they open
  with the suite it installs, and two installs can't run at once. It gives the name up once
  it starts opening the documents or closes, so later launches open their own dialog
  instead of handing over documents that would never be opened.
- **Related refs:** errors libflatpak marks non-fatal (translations, GL extensions) are
  skipped and shown as progress, so they don't abort the install.
- **PackageKit version:** the D-Bus enum numbers are PackageKit 1.x's (its main branch
  renumbers them), so the installer checks the daemon's `VersionMajor` and refuses anything else.

## Packaging

Everything is in the `ultramarine-office-installer` CMake component. Building needs
`flatpak-devel` (libflatpak, found with pkg-config). At runtime the package needs
`flatpak-libs` (pulled in by the soname) and `PackageKit` (the daemon, not `pkcon`), and
Flathub has to be configured as the `flathub` system remote.

## Testing

```bash
just build
build/bin/ultramarine-office-installer ~/Documents/report.docx
```

With an office suite installed, that opens the document in the suite. Run it without
a file to always see the dialog.
