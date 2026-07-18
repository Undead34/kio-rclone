# KIO Rclone

📖 Documentación completa en español: **[undead34.github.io/kio-rclone](https://undead34.github.io/kio-rclone/)**
(English docs: [undead34.github.io/kio-rclone/en/](https://undead34.github.io/kio-rclone/en/))

Browse the remotes already configured in [rclone](https://rclone.org/) from
Dolphin and every other KIO-aware application.

`KIO Rclone` exposes a normal-looking `rclone:/` location. It deliberately
does **not** depend on KDE Online Accounts or LibKGAPI: rclone owns OAuth,
provider APIs, retries and the user's credentials.

## What it is for

Use it when you want a cloud remote to behave like a native Dolphin location
without giving the file manager a second, provider-specific account database.
The same rclone configuration can still be used from the terminal, scripts,
mounts or another frontend.

| Capability | Status |
| --- | --- |
| Browse remotes and folders | Available |
| Download and upload through Dolphin | Available |
| Create, rename and delete files/directories | Available |
| Open and edit ordinary documents through KIOFuse | ⚠️ Not recommended yet — copy locally, edit, upload back |
| Pause and cancel transfers | Available through KIO backpressure |
| Google Drive private OAuth flow | Available in **Rclone Remotes** |
| Full offline cache, sync UI or mount manager | Not part of this project |

## Quick start

1. Install rclone and configure at least one remote.
2. Install KIO Rclone.
3. Open `rclone:/` in Dolphin's location bar.
4. Select **Configure Remotes…** inside that location, or launch **Rclone
   Remotes** from the application menu.

For Google Drive, configure a private OAuth client before doing serious work.
The rclone shared client is quota-limited and rclone warns that it is being
retired during 2026. The configuration utility detects this situation and
offers **Google OAuth…**. If this machine already has an OAuth override, the
dialog can import it; authentication still happens directly through rclone.

Read rclone's [Google Drive OAuth client guide](https://rclone.org/drive/#making-your-own-client-id)
before creating credentials.

## Installation

### Arch Linux

Build and install the tracked native package:

~~~bash
cd packaging/arch
makepkg -si
kbuildsycoca6 --noincremental
~~~

To replace an older package with a package already built in this checkout:

~~~bash
sudo pacman -U ./kio-rclone-0.3.1-1-x86_64.pkg.tar.zst
kbuildsycoca6 --noincremental
~~~

Close and reopen Dolphin if it already has an idle KIO worker from an older
version. Uninstall with:

~~~bash
sudo pacman -Rns kio-rclone
~~~

Other distributions are planned; see the [distribution plan](docs/distribution-plan.md).

### Development install

~~~bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
source build/prefix.sh
kioclient ls rclone:/
~~~

The environment script matters for a user-local install because Qt does not
search arbitrary prefixes for KIO plugins. A distribution package installs the
worker in Qt's system plugin directory instead.

## Using it in Dolphin

Open one of these URLs in the location bar:

| URL | Meaning |
| --- | --- |
| `rclone:/` | All configured rclone remotes |
| `rclone:/Google%20Drive/` | Root of a remote named `Google Drive` |
| `rclone:/Google%20Drive/Projects/report.pdf` | One remote file |

Normal Dolphin actions work where the provider supports them: drag and drop,
copy/paste, new folder, rename, delete, Properties and F5 refresh. A rename
inside the same remote uses rclone's native move operation. A copy between
different locations is intentionally streamed by KIO so that Pause and Cancel
remain meaningful.

### Transfer behaviour

Uploads stream to a unique staging name and publish with `moveto` only after
the transfer succeeds, so a cancelled or failed upload never overwrites the
previous remote file; downloads and progress reporting have similar safety
details. See [Transfers, pause, and progress](docs/transfers.md) for the full
breakdown.

### Opening and editing documents

> **Do not edit documents in place through `rclone:/` yet.** Applications
> that need a local seekable file, including LibreOffice and text editors,
> normally reach `rclone:/` through KIOFuse. This path is not battle-tested
> and has caused real data loss during development. Until KIO Rclone has a
> more robust editing story (tracked as a future improvement, not yet
> implemented), treat it as read-mostly: **copy the file to a local path,
> edit the local copy, then upload the result back yourself.**

Applications that need a local seekable file normally reach `rclone:/`
through KIOFuse. KIO Rclone deliberately keeps its KIO `opening` capability
disabled so KIOFuse provides the whole-file cache instead; in principle,
ordinary remote files are uploaded back when that cache is flushed, but this
path has not been reliable enough in practice to trust with anything you
cannot afford to lose.

Two cloud cases are additionally read-only:

- Google-native documents exported by rclone have no stable size until they
  are downloaded. KIO Rclone materializes them so LibreOffice receives the
  correct bytes and size, but does not replace the native collaborative
  document with an uploaded Office file.
- Some providers allow several objects with the same name. KIO Rclone shows
  the newest one and, when the backend exposes an object ID, downloads that
  exact object instead of concatenating duplicates. Resolve the duplicate
  names in the provider before editing that path.

This release does not implement reliable in-place document editing, remote
random-access I/O, or an offline synchronization cache.

## Configuration and credentials

The configuration application only operates on rclone's normal configuration
and never copies OAuth tokens into KIO Rclone's own settings. For Google
Drive, set up a private OAuth client from **Rclone Remotes** → **Google
OAuth…**; see [Google Drive and GCP](docs/google-drive.md) for the full
walkthrough. Never share a client secret, token, or unredacted rclone config
in an issue — use `rclone config redacted` instead.

## Requirements

Runtime:

- rclone
- Qt 6
- KDE Frameworks 6: KIO, KCoreAddons and KI18n

Build-time minimums are Qt 6.5, KDE Frameworks 6.3, CMake 3.20, ECM and Ninja.
The Arch package declares the exact runtime dependencies in
[packaging/arch/PKGBUILD](packaging/arch/PKGBUILD).

## Architecture

~~~text
Dolphin / any KIO client
          │ rclone:/ URLs
          ▼
      KIO worker
          │ streams bytes and KIO notifications
          ▼
      rclone process
          │ OAuth, retries and provider APIs
          ▼
     cloud remote
~~~

The worker never implements Google Drive, S3, WebDAV or another provider API
itself. That keeps provider behaviour aligned with rclone and makes the worker
useful for every rclone backend that supports the requested operation.

## User documentation website

The `docs/` directory is also a VitePress static website. It contains the
English and Spanish user guides, the Google Cloud setup guide, and project
documentation. Read the [English documentation](https://undead34.github.io/kio-rclone/en/)
or the [Spanish documentation](https://undead34.github.io/kio-rclone/).
The published copy is [undead34.github.io/kio-rclone](https://undead34.github.io/kio-rclone/).

```bash
pnpm install
pnpm docs:dev
pnpm docs:build
pnpm docs:preview
```

`docs:build` produces deployable static files in `docs/.vitepress/dist/`; no
application server, database or credentials are needed to host them.

## Testing

~~~bash
ctest --test-dir build --output-on-failure
~~~

The automated suite is deliberately cloud-free (URL parsing, rclone JSON
parsing, local-backend integration, transfer pause/progress) and also runs in
the Arch package's `check()`. See [Testing KIO Rclone](docs/testing.md) for
the full automated and manual test matrix.

## Diagnostics and support

KIO Rclone has no telemetry and never writes OAuth tokens to its own files.
See [Logs and safe diagnostics](docs/logging.md) for what's safe to collect
and share in a bug report.

## Scope and known boundaries

KIO Rclone is a browser and transfer bridge, not a replacement for rclone
mount, sync, bisync or a full VFS cache. In particular:

- There is no background synchronization daemon.
- There is no offline cache or queued transfer manager.
- Provider features not exposed by KIO stay available through rclone itself.
- Cloud providers may allow duplicate filenames even though a file-manager URL
  normally identifies one path. KIO Rclone exposes only the newest duplicate
  and keeps that ambiguous path read-only until the duplicates are resolved.

## Project plan

The next release work is documented rather than implied:

- [Distribution and release plan](docs/distribution-plan.md)
- [Testing strategy](docs/testing.md)
- [Logging and support diagnostics](docs/logging.md)

## Contributing

Keep commits small and functional. Run the test suite before proposing a
change, and do not add a credential, token, real remote name or private path to
the repository. The project is licensed under GPL-2.0-or-later; source files
carry SPDX headers.
