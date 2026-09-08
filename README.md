# KIO Rclone

Browse the remotes already configured in [rclone](https://rclone.org/) from
Dolphin and other KIO-aware applications through `rclone:/`.

rclone remains responsible for providers, OAuth, retries, and credentials. KIO
Rclone adds the Dolphin/KIO integration; it does not create another account
database, synchronization service, or mount manager.

**Documentation:** [English](https://undead34.github.io/kio-rclone/) ·
[Español](https://undead34.github.io/kio-rclone/es/)

## Installation

On Arch Linux, install [`kio-rclone`](https://aur.archlinux.org/packages/kio-rclone)
from the AUR:

~~~bash
yay -S kio-rclone
kbuildsycoca6 --noincremental
~~~

For a source build, a local development prefix, or system-integration details,
use the [development guide](docs/development.md). Do not mix an unmanaged
`/usr` source install with the AUR package.

## Use

1. Configure a remote with `rclone config` or **Rclone Remotes**.
2. Enter `rclone:/` in Dolphin's location bar.
3. Use **Configure Remotes…** at the root whenever the remote configuration
   needs to change.

For Google Drive, use a private OAuth client before regular use; the shared
rclone client is quota-limited. Follow the
[Google Drive guide](https://undead34.github.io/kio-rclone/google-drive).

## Documentation

- [Features and limitations](https://undead34.github.io/kio-rclone/features)
- [Transfers, pause, and safe publishing](https://undead34.github.io/kio-rclone/transfers)
- [Troubleshooting](https://undead34.github.io/kio-rclone/troubleshooting)
- [Safe diagnostics](https://undead34.github.io/kio-rclone/logging)
- [Development](docs/development.md), [testing](docs/testing.md), and the
  [release procedure](docs/releasing.md)

> [!WARNING]
> In-place document editing through `rclone:/` is not yet reliable enough for
> important files. Copy them locally before editing. See
> [Transfers](https://undead34.github.io/kio-rclone/transfers).

## Support

Report issues at [GitHub](https://github.com/Undead34/kio-rclone/issues). Include
the exact action, visible error, Plasma/KDE Frameworks/rclone versions, and
`rclone config redacted`; never include tokens, client secrets, or an
unredacted configuration.

## License

[GPL-2.0-or-later](https://spdx.org/licenses/GPL-2.0-or-later.html)
