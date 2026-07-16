# KIO Rclone

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
sudo pacman -U ./kio-rclone-0.2.0-1-x86_64.pkg.tar.zst
kbuildsycoca6 --noincremental
~~~

Close and reopen Dolphin if it already has an idle KIO worker from an older
version. Uninstall with:

~~~bash
sudo pacman -Rns kio-rclone
~~~

Other distributions are planned; see the [distribution plan](docs/DISTRIBUTION_PLAN.md).

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

| Situation | What KIO Rclone does |
| --- | --- |
| Download | Streams one requested remote file without a redundant metadata pass. |
| Upload | Streams stdin to `rclone rcat` with the exact size when KIO knows it. |
| Progress | Shows rclone's remote upload percentage, rate and ETA in the notification. |
| Final remote commit | Shows **Finalizing upload…** until rclone confirms success. |
| Pause | Stops KIO's producer/consumer flow, so rclone stops receiving data. |

Dolphin can briefly show a 100% bar while a cloud provider commits its final
chunk. This is a KIO `FileCopyJob` detail: its main byte counter follows the
source side of the stream. The notification text remains the authoritative
state until the job completes.

Google Drive's default upload chunk is 8 MiB; larger chunks improve throughput
at the cost of memory. KIO Rclone does not override that provider setting with
a smaller value. See [rclone's Drive options](https://rclone.org/drive/#drive-chunk-size)
and [`rcat` documentation](https://rclone.org/commands/rclone_rcat/).

## Configuration and credentials

The configuration application only operates on rclone's normal configuration.
It does not copy OAuth tokens into KIO Rclone settings.

For Google Drive:

1. Launch `kio-rclone-config`.
2. Select the Drive remote.
3. Choose **Google OAuth…**.
4. Enter your Google OAuth desktop client credentials, or import the existing
   local override when offered.
5. Complete the browser authorization opened by rclone.

The client secret and token must never be pasted into an issue or a support
chat. Use `rclone config redacted` when a redacted configuration is needed.

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

## Testing

The automated suite is deliberately cloud-free:

- URL parsing and URL construction
- rclone JSON listing/config parsing
- local-backend integration
- transfer pause/backpressure for both download and upload
- live upload progress and finalization messages

Run it with:

~~~bash
ctest --test-dir build --output-on-failure
~~~

The Arch package runs the same suite in `check()`. The full manual and release
test matrix is in [docs/TESTING.md](docs/TESTING.md).

## Diagnostics and support

KIO Rclone has no telemetry and does not write OAuth tokens to its own files.
Provider errors are returned to Dolphin; rclone handles credentials in its own
configuration.

For a useful, safe bug report, collect the rclone version, a redacted config,
the exact operation and the visible KIO error. Do **not** share access tokens,
client secrets or an unredacted rclone config. See [docs/LOGGING.md](docs/LOGGING.md).

## Scope and known boundaries

KIO Rclone is a browser and transfer bridge, not a replacement for rclone
mount, sync, bisync or a full VFS cache. In particular:

- There is no background synchronization daemon.
- There is no offline cache or queued transfer manager.
- Provider features not exposed by KIO stay available through rclone itself.
- Cloud providers may allow duplicate filenames even though a file-manager URL
  normally identifies one path; avoid duplicate names in the same folder.

## Project plan

The next release work is documented rather than implied:

- [Distribution and release plan](docs/DISTRIBUTION_PLAN.md)
- [Testing strategy](docs/TESTING.md)
- [Logging and support diagnostics](docs/LOGGING.md)

## Contributing

Keep commits small and functional. Run the test suite before proposing a
change, and do not add a credential, token, real remote name or private path to
the repository. The project is licensed under GPL-2.0-or-later; source files
carry SPDX headers.
