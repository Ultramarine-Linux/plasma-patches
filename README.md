# Ultramarine Plasma Settings

Maintained by Cypress Reed

Plasma System Settings modules and Discover integration for Ultramarine Linux.

- **Updates** (`kcm_ultramarine_updates`): system (RPM or bootc image), Flatpak and Nix updates, backed by the
  `ultramarine-updates` helper in [`updater/`](updater).
- **Update helper** (`ultramarine-updates`): lists and installs updates, and runs automatic updates and Apply Tonight.
- **Tweaks** (`kcm_ultramarine_tweaks`): optional Ultramarine system tweaks, backed by
  [`umcli`](https://github.com/Ultramarine-Linux/um).
- **Discover patch** (`discover/`): routes Discover's Updates entry to the Ultramarine Updates KCM.
- **Launch Using Dedicated Graphics Card** (`dgpu-launch/`, `plasma-desktop/`): app launcher and task manager
  action that runs an app on the discrete GPU, only shown on hybrid GPU systems and with an eGPU.
- **Office suite installer** (`office-installer/`): opening an office document with no office suite installed offers
  to install one: Collabora Office (recommended), LibreOffice or ONLYOFFICE, from Flathub by default.

## Documentation

- [Build and test](docs/building.md)
- [Update helper, automatic updates and runtime requirements](docs/updates-backend.md)
- [umcli backend (Tweaks)](docs/umcli-backend.md)
- [Discover patch and packaging](docs/discover.md)
- [Launch Using Dedicated Graphics Card](docs/dgpu-launch.md)
- [Office suite installer](docs/office-installer.md)
- [Translations with Weblate](docs/translations.md)
