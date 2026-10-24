# Ultramarine Plasma Settings

Maintained by Cypress Reed

Plasma System Settings modules and Discover integration for Ultramarine Linux.

- **Updates** (`kcm_ultramarine_updates`): system (RPM or bootc image), Flatpak and Nix updates, backed by the
  `ultramarine-updates` helper in [`updater/`](updater).
- **Update helper** (`ultramarine-updates`): lists and installs updates, and runs automatic updates and Apply Tonight.
- **Tweaks** (`kcm_ultramarine_tweaks`): optional Ultramarine system tweaks, backed by
  [`umcli`](https://github.com/Ultramarine-Linux/um).
- **Discover patch** (`discover/`): routes Discover's Updates entry to the Ultramarine Updates KCM.

## Documentation

- [Build and test](docs/building.md)
- [Update helper, automatic updates and runtime requirements](docs/updates-backend.md)
- [umcli backend (Tweaks)](docs/umcli-backend.md)
- [Discover patch and packaging](docs/discover.md)
- [Translations with Weblate](docs/translations.md)
