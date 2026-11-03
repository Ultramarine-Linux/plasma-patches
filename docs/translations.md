# Translations with Weblate

The Updates, Tweaks and Location Services KCMs, the office suite installer and the
dGPU launcher use
KI18n gettext catalogs. C++ and QML strings use their component's translation
domain; generated English templates are kept in `po/`.

After changing user-visible strings, regenerate the templates:

```bash
just translations
```

This writes:

- `po/kcm_ultramarine_updates.pot`
- `po/kcm_ultramarine_tweaks.pot`
- `po/kcm_ultramarine_location.pot`
- `po/ultramarine_office_installer.pot`
- `po/ultramarine_dgpu_launch.pot`

## Weblate components

Create five Gettext PO components in the Weblate project, with English (`en`) as
the source language:

| Component | Template | File mask |
| --- | --- | --- |
| Updates KCM | `po/kcm_ultramarine_updates.pot` | `po/*/kcm_ultramarine_updates.po` |
| Tweaks KCM | `po/kcm_ultramarine_tweaks.pot` | `po/*/kcm_ultramarine_tweaks.po` |
| Location Services KCM | `po/kcm_ultramarine_location.pot` | `po/*/kcm_ultramarine_location.po` |
| Office suite installer | `po/ultramarine_office_installer.pot` | `po/*/ultramarine_office_installer.po` |
| dGPU launcher | `po/ultramarine_dgpu_launch.pot` | `po/*/ultramarine_dgpu_launch.po` |

Weblate writes translations to `po/<locale>/<domain>.po`. CMake's
`ki18n_install(po)` compiles those files into the matching KI18n domains. It is
enabled only when at least one translated PO exists, so untranslated builds do
not install an empty locale directory.

The CMake build requires `gettext` once translation catalogs are present. The
KCM RPM should also add `BuildRequires: gettext` and collect all five domains with
`%find_lang` so translated `.mo` files are included in the package. Since the
initial source contains templates but no translated PO files, the package spec
should tolerate `%find_lang` finding no files until Weblate adds translations.

Weblate project creation and repository access are configured in the Weblate
instance, not in this source tree; a project admin needs to add the five
components there.

## Known limitation: metadata files

`Messages.sh` only extracts `i18n()` calls from C++ and QML. These strings are
not extracted and stay English-only:

- `Name` and `Comment` in the desktop files (the dGPU launcher action, the office
  suite installer and the update helper's autostart entry)
- `Name`, `Description` and keywords in the KCMs' JSON metadata
  (`kcm-*/kcm_ultramarine_*.json`), which System Settings shows in its sidebar and search
- polkit `<message>` and `<description>` strings in the `.policy.in` files, which
  appear in the authentication dialog

KDE's own repositories don't use `Messages.sh` for these. KDE's translation
tooling ([scripty](https://invent.kde.org/sysadmin/l10n-scripty)) extracts
desktop files and KPlugin JSON metadata itself, and writes the translated
`Name[de]=`-style keys back into the source files. Outside KDE's infrastructure the
usual route is gettext's own support: `xgettext` extracts desktop files directly
and polkit policies through the ITS rules polkit ships (`polkit.its`), and
`msgfmt --desktop` / `msgfmt --xml` merge the translations back at build time.
JSON metadata has no gettext merge tool, so it would need a small script.
