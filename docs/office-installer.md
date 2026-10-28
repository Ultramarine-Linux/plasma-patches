# Office suite installer

Ultramarine doesn't install an office suite by default. When no suite is installed,
opening an office document (ODF, OOXML, legacy Microsoft Office, iWork, WordPerfect)
shows a dialog that offers to install one:

- **Collabora Office** (recommended): Flathub only. It isn't packaged in Fedora or Terra.
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
  It's `NoDisplay`, so it isn't in the app launchers. Formats that are also plain text
  (CSV, RTF, flat ODF) are left out, so they still open in a text editor.
- **Once a suite is installed:** KF6 ignores `InitialPreference`. Handlers that aren't
  in a `mimeapps.list` all have the same preference, and KService sorts them by desktop
  file name, so `org.ultramarine.OfficeInstaller` comes after `com.collaboraoffice.*`,
  `libreoffice-*`, `org.libreoffice.*` and `org.onlyoffice.*`. Fedora's `mimeapps.list`
  also makes `libreoffice-*.desktop` the default. When the installer still gets picked
  (a suite that sorts later, like WPS, or GIO, which doesn't rank handlers), it looks
  for another app that lists the document's MIME type itself. If it finds one, it opens
  the document there without showing anything. Parent types don't count: a `.docx` is a
  zip, but Ark isn't an office suite.
- **Installing:** Flatpaks install with `flatpak install --system flathub <id>`, and RPMs
  with `pkcon install`. Both authorize through polkit, which prompts in the session. The
  dialog can't be closed while installing, so the install isn't left half done.
  Afterwards the installer refreshes KSycoca and opens the documents with the new suite.
  If KSycoca doesn't know about the suite yet, it runs the suite directly
  (`flatpak run --file-forwarding` or `libreoffice`).

## Packaging

Everything is in the `ultramarine-office-installer` CMake component. The package needs
`flatpak` and `PackageKit` (for `pkcon`) at runtime, and Flathub has to be configured
as the `flathub` system remote.

## Testing

```bash
just build
build/bin/ultramarine-office-installer ~/Documents/report.docx
```

With an office suite installed, that opens the document in the suite. Run it without
a file to always see the dialog.
