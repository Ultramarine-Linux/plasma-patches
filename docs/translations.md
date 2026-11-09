# Translations with Weblate

Everything in this repository is translated through one gettext catalog, so Weblate
has one component and translators see every string in one place.

## Updating the template

After changing user-visible strings, regenerate the template and commit it:

```bash
just translations
```

This runs `Messages.sh`, which writes `po/ultramarine-plasma-settings.pot`. It finds its
sources by type among the tracked files, so new files are picked up without editing it:

| Source | Strings |
| --- | --- |
| `*.cpp`, `*.h`, `*.qml` (not `tests/`) | `i18n()`, `i18nc()`, `i18np()` and `i18ncp()` calls |
| `*.desktop.in` | `Name`, `GenericName`, `Comment` and `Keywords` |
| `*.policy.in` | polkit `<description>` and `<message>` |
| `kcm-*/*.json.in` | KCM `Name`, `Description` and `X-KDE-Keywords` |

References in the template name the file but not the line, and `POT-Creation-Date` only
changes along with the strings. The template therefore only changes when the strings do.
CI regenerates it and fails when the committed copy is out of date.

`Messages.sh` needs `git`, `gettext`, `python3` and `polkit-devel` (for polkit's ITS
rules, `/usr/share/gettext/its/polkit.its`, or set `POLKIT_ITS`).

## Weblate component

One Gettext PO component, with English (`en`) as the source language:

| Setting | Value |
| --- | --- |
| Template for new translations | `po/ultramarine-plasma-settings.pot` |
| File mask | `po/*.po` |
| Monolingual base language file | (none, bilingual PO) |

There's no `LINGUAS` file to maintain. The build makes one from the `po/*.po` it finds.

## What the build does with it

`cmake/UltramarineTranslations.cmake` compiles each `po/<lang>.po` once, then:

- **installs the `.mo` under each runtime translation domain.** A KCM's QML always uses
  the KCM's plugin ID as its domain (`KQuickConfigModule` sets it), so the KCMs keep
  `kcm_ultramarine_updates`, `kcm_ultramarine_tweaks` and `kcm_ultramarine_location`.
  The update helper, dGPU launcher and office suite installer share
  `ultramarine-plasma-settings`. Every domain gets the whole catalog. It's small, and
  this way nothing has to work out which strings belong to which domain.
- **merges translations into the metadata.** Desktop files go through
  `msgfmt --desktop` and polkit policies through `msgfmt --xml`, which adds the
  `Name[de]=` keys and `xml:lang` elements. gettext has no JSON merge, so
  `cmake/kcm-json-l10n.py` adds `"Name[de]"`-style keys to the KCM metadata. moc embeds
  that JSON in the plugin, so the KCM sources are `*.json.in` and the translated
  `*.json` is generated into the build directory, where `K_PLUGIN_CLASS_WITH_JSON`
  finds it. The KCMs' desktop files are generated from the translated JSON too.

With no `po/<lang>.po` yet, the files are installed untranslated and no `.mo` is
installed.

Adding a translatable file type means a rule in `Messages.sh` and a merge step in
`cmake/UltramarineTranslations.cmake` (`um_translate_desktop_file`, `um_translate_policy`
and `um_translate_kcm_metadata` cover the current ones).

## Packaging

The spec needs `BuildRequires: gettext`, `python3` and `polkit-devel`. All four domains
come from the same catalog and are noarch data, so `-common` (which every subpackage
requires) owns them through `%find_lang`. That way, for example, the update helper's
notifications are translated even when the Updates KCM isn't installed.
