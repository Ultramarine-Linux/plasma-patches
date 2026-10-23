# Translations

The KCMs use KI18n gettext catalogs. Regenerate the English templates after
changing a user-visible string:

```sh
just translations
```

The command writes one POT per translation domain:

- `kcm_ultramarine_updates.pot`
- `kcm_ultramarine_tweaks.pot`

Weblate should use these as two separate Gettext PO components, with English as
the source language:

| Component | Template | File mask |
| --- | --- | --- |
| Updates KCM | `po/kcm_ultramarine_updates.pot` | `po/*/kcm_ultramarine_updates.po` |
| Tweaks KCM | `po/kcm_ultramarine_tweaks.pot` | `po/*/kcm_ultramarine_tweaks.po` |

Weblate writes catalogs to `po/<locale>/<domain>.po`. CMake's `ki18n_install(po)`
compiles and installs them into the matching KI18n translation domains. The
package should use `%find_lang` for both domains once translated PO files are
present. No empty English catalog is needed; the source strings are the English
fallback.
