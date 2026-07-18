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

Pick one method. Each install has a matching uninstall that fully reverses it.
Every install and uninstall ends with `kbuildsycoca6 --noincremental`, which
rebuilds the KDE service cache so `rclone:/` is registered or dropped.

Other distributions are planned; see the
[distribution plan](docs/distribution-plan.md).

### Arch Linux (AUR) — recommended

Install [`kio-rclone`](https://aur.archlinux.org/packages/kio-rclone) with your
helper of choice:

~~~bash
yay -S kio-rclone
kbuildsycoca6 --noincremental
~~~

Uninstall:

~~~bash
yay -Rns kio-rclone
kbuildsycoca6 --noincremental
~~~

### From source, system-wide (`/usr`)

Needs root, installs where Qt already looks (no environment setup). Do not mix
with the AUR package — see [/usr conflict recovery](#usr-conflict-recovery).

Install:

~~~bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
kbuildsycoca6 --noincremental
~~~

Uninstall (removes exactly the files the install wrote):

~~~bash
sudo xargs rm -v < build/install_manifest.txt
kbuildsycoca6 --noincremental
~~~

### From source, user-only (`$HOME/.local`)

No root. Qt does not scan `~/.local` for plugins, so the worker and the config
app need `QT_PLUGIN_PATH` and `PATH` set for your session; a logout/login
applies them.

Install:

~~~bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build

mkdir -p ~/.config/environment.d
cat > ~/.config/environment.d/kio-rclone.conf <<'EOF'
QT_PLUGIN_PATH=${HOME}/.local/lib/qt6/plugins:${QT_PLUGIN_PATH}
PATH=${HOME}/.local/bin:${PATH}
EOF

# log out and back in, then:
kbuildsycoca6 --noincremental
~~~

Uninstall:

~~~bash
xargs rm -v < build/install_manifest.txt
rm ~/.config/environment.d/kio-rclone.conf
kbuildsycoca6 --noincremental
# log out and back in
~~~

### /usr conflict recovery

A `sudo cmake --install` into `/usr` leaves files that no pacman package owns,
so a later `yay -S kio-rclone` fails with `conflicting files`. Let pacman take
ownership of them:

~~~bash
sudo pacman -U --overwrite '/usr/*' <path>/kio-rclone-*-x86_64.pkg.tar.zst
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
network entry · `autotests/` test suite · `po/` translations · `docs/`
documentation site. Arch packaging lives in a separate AUR repository.

## Contributing

Bug reports and pull requests are welcome at
[github.com/Undead34/kio-rclone](https://github.com/Undead34/kio-rclone/issues).
When sharing rclone configuration in a report, use `rclone config redacted`.

## License

[GPL-2.0-or-later](https://spdx.org/licenses/GPL-2.0-or-later.html)
