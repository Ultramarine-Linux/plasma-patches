# Translations

everything in this repo gets translated through one gettext catalog, so weblate only has one component and translators see every string in one place.

## updating the template

after changing user-visible strings, regenerate the template and commit it:

```bash
just translations
```

this runs `Messages.sh`, which writes `po/ultramarine-plasma-settings.pot`. it finds its sources by type among the tracked files, so new files get picked up without touching it:

| source | strings |
| --- | --- |
| `*.cpp`, `*.h`, `*.qml` (not `tests/`) | `i18n()`, `i18nc()`, `i18np()` and `i18ncp()` calls |
| `*.desktop.in` | `Name`, `GenericName`, `Comment` and `Keywords` |
| `*.policy.in` | polkit `<description>` and `<message>` |
| `*.json.in` | KCM `Name`, `Description` and `X-KDE-Keywords` |

references in the template name the file but not the line, and `POT-Creation-Date` only changes along with the strings, so the template only changes when the strings do. CI regenerates it and fails if the committed copy is out of date.

`Messages.sh` needs `git`, `gettext`, `python3` and `polkit-devel` (for polkit's ITS rules at `/usr/share/gettext/its/polkit.its`, or set `POLKIT_ITS`).

## weblate

one gettext PO component, with english (`en`) as the source language:

| setting | value |
| --- | --- |
| template for new translations | `po/ultramarine-plasma-settings.pot` |
| file mask | `po/*.po` |
| monolingual base language file | (none, bilingual PO) |

there's no `LINGUAS` file to maintain, the build makes one from whatever `po/*.po` it finds.

## what the build does with it

`cmake/UltramarineTranslations.cmake` compiles each `po/<lang>.po` once, then:

- **installs the `.mo` under every runtime translation domain.** a KCM's QML always uses the KCM's plugin ID as its domain (`KQuickConfigModule` sets that), so the KCMs keep `kcm_ultramarine_updates`, `kcm_ultramarine_tweaks` and `kcm_ultramarine_location`. the update helper, dGPU launcher and office installer share `ultramarine-plasma-settings`. every domain gets the whole catalog, which is small, so nothing has to figure out which strings belong where
- **merges translations into the metadata.** desktop files go through `msgfmt --desktop` and polkit policies through `msgfmt --xml`. gettext can't merge JSON, so `cmake/kcm-json-l10n.py` adds `"Name[de]"`-style keys to the KCM metadata. moc embeds that JSON in the plugin, so the KCM sources are `*.json.in` and the translated `*.json` gets generated into the build directory, where `K_PLUGIN_CLASS_WITH_JSON` finds it. the KCMs' desktop files are generated from the translated JSON too

with no `po/<lang>.po` yet, everything gets installed untranslated and no `.mo` is installed.

adding a new translatable file type means adding a rule in `Messages.sh` and a merge step in `cmake/UltramarineTranslations.cmake` (`um_translate_desktop_file`, `um_translate_policy` and `um_translate_kcm_metadata` cover the current ones).

## packaging

the spec needs `BuildRequires: gettext`, `python3` and `polkit-devel`. all four domains come from the same catalog and are noarch data, so `-common` (which every subpackage requires) owns them through `%find_lang`. that way the update helper's notifications are translated even when the updates KCM isn't installed.
