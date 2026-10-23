# Translations with Weblate

The Updates and Tweaks KCMs use KI18n gettext catalogs. C++ and QML strings use
their KCM translation domain; generated English templates are kept in `po/`.

After changing user-visible strings, regenerate the templates:

```bash
just translations
```

This writes:

- `po/kcm_ultramarine_updates.pot`
- `po/kcm_ultramarine_tweaks.pot`

## Weblate components

Create two Gettext PO components in the Weblate project, with English (`en`) as
the source language:

| Component | Template | File mask |
| --- | --- | --- |
| Updates KCM | `po/kcm_ultramarine_updates.pot` | `po/*/kcm_ultramarine_updates.po` |
| Tweaks KCM | `po/kcm_ultramarine_tweaks.pot` | `po/*/kcm_ultramarine_tweaks.po` |

Weblate writes translations to `po/<locale>/<domain>.po`. CMake's
`ki18n_install(po)` compiles those files into the matching KI18n domains. It is
enabled only when at least one translated PO exists, so untranslated builds do
not install an empty locale directory.

The CMake build requires `gettext` once translation catalogs are present. The
KCM RPM should also add `BuildRequires: gettext` and collect both domains with
`%find_lang` so translated `.mo` files are included in the package. Since the
initial source contains templates but no translated PO files, the package spec
should tolerate `%find_lang` finding no files until Weblate adds translations.

Weblate project creation and repository access are configured in the Weblate
instance, not in this source tree; a project admin needs to add the two
components there.
