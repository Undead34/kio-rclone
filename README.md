# KIO Rclone

Browse the remotes already configured in [rclone](https://rclone.org/) from
Dolphin and every other KIO-aware application, through an `rclone:/`
location.

rclone owns OAuth, provider APIs, retries and credentials — KIO Rclone adds no
second account database and works with every rclone backend. The same
configuration remains usable from the terminal, scripts or mounts.

**Documentation:** [🇪🇸 Español](https://undead34.github.io/kio-rclone/es/) · [🇺🇸 English](https://undead34.github.io/kio-rclone/)

## Features

- Browse, download, upload, create, rename and delete through Dolphin, with
  pause and cancel.
- Safe transfers: uploads publish atomically only after they complete, so a
  failed transfer never overwrites the previous remote file.
- **Rclone Remotes**, a graphical manager to add, edit and reconnect remotes
  (Google Drive, OneDrive, Dropbox, plus rclone's wizard for everything else).
- Translated UI (English, Spanish).

> [!WARNING]
> In-place document editing through `rclone:/` (via KIOFuse) is not yet
> reliable and can lose data. Copy files locally to edit them.
> [Details](https://undead34.github.io/kio-rclone/transfers).

KIO Rclone is a browser and transfer bridge, not a sync client: there is no
offline cache, background synchronization or mount manager.

## Installation

### Arch Linux

~~~bash
cd packaging/arch
makepkg -si
kbuildsycoca6 --noincremental
~~~

Other distributions are planned; see the
[distribution plan](docs/distribution-plan.md).

### From source

~~~bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
source build/prefix.sh   # user-local installs only: Qt must find the worker
~~~

## Quick start

1. Configure at least one rclone remote (`rclone config` or **Rclone Remotes**).
2. Open `rclone:/` in Dolphin's location bar.

Google Drive requires a private OAuth client for regular use: rclone's shared
client is quota-limited and scheduled for retirement in 2026. **Rclone
Remotes** can set one up; see the
[Google Drive guide](https://undead34.github.io/kio-rclone/google-drive).

## Development

- Tests: `ctest --test-dir build --output-on-failure` (no cloud access
  required; see [testing](docs/testing.md)).
- Documentation site (VitePress): `cd docs && pnpm install && pnpm docs:dev`.
- Translations: edit `po/<lang>/kio6_rclone.po`; after changing `i18n()`
  strings run `scripts/update-translations.sh`.

Layout: `src/` KIO worker · `app/` Rclone Remotes GUI · `desktop/` Dolphin
network entry · `autotests/` test suite · `po/` translations ·
`packaging/` distro packaging · `docs/` documentation site.

## Contributing

Bug reports and pull requests are welcome at
[github.com/Undead34/kio-rclone](https://github.com/Undead34/kio-rclone/issues).
When sharing rclone configuration in a report, use `rclone config redacted`.

## License

[GPL-2.0-or-later](https://spdx.org/licenses/GPL-2.0-or-later.html)
