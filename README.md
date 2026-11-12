# Ultramarine Plasma Settings

maintained by Cypress Reed

this is where Ultramarine's plasma system settings modules, plasma patches and other little desktop integrations live.

- **Updates** (`kcm_ultramarine_updates`): system (RPM or bootc image), flatpak, nix and plasmoid updates, backed by the `ultramarine-updates` helper in [`updater/`](updater). the helper also runs automatic updates and Apply Tonight
- **Tweaks** (`kcm_ultramarine_tweaks`): optional Ultramarine system tweaks, backed by [`umcli`](https://github.com/Ultramarine-Linux/um)
- **Location Services** (`kcm_ultramarine_location`): turns geoclue on or off for the whole system, since plasma doesn't have a switch for it
- **Discover patch** (`discover/`): sends discover's updates entry to our updates KCM instead
- **PowerDevil patch** (`powerdevil/`): backports upstream powerdevil MR !699 so plasma tells logind when a session is idle (`IdleHint`), which automatic updates use to tell whether the device is in use
- **Launch Using Dedicated Graphics Card** (`dgpu-launch/`): a launcher action that runs an app on the dGPU, only shown on hybrid GPU systems (eGPUs included)
- **Office suite installer** (`office-installer/`): opening an office document with no office suite installed offers to install Collabora Office (recommended), LibreOffice or ONLYOFFICE, from flathub by default

## docs

- [building and testing](docs/building.md)
- [updates KCM and the update helper](docs/updates-backend.md)
- [tweaks KCM](docs/umcli-backend.md)
- [location KCM](docs/location.md)
- [discover patches](docs/discover.md)
- [powerdevil idle hint](docs/powerdevil.md)
- [dGPU launch](docs/dgpu-launch.md)
- [office app installer](docs/office-installer.md)
- [translations](docs/translations.md)
