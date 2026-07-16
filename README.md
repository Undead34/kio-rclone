# KIO Rclone

KIO Rclone exposes rclone remotes as `rclone:/` URLs in Dolphin and other
KIO-aware applications.

The project deliberately does not use KDE Online Accounts or LibKGAPI.
Authentication, cloud APIs, retries, and provider-specific behavior are
handled by the user's rclone installation.

## Install on Arch Linux

The recommended installation is a small native package, so every installed
file is tracked by pacman:

```bash
cd packaging/arch
makepkg -si
kbuildsycoca6 --noincremental
```

Open `rclone:/` in Dolphin or launch **Rclone Remotes** from the application
menu. Uninstall with:

```bash
sudo pacman -Rns kio-rclone
```

## Development build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
source build/prefix.sh
kioclient ls rclone:/
```

The prefix environment is required for a user-local development install
because Qt plugins are not searched below arbitrary prefixes automatically.
The Arch package installs the worker in Qt's system plugin directory and does
not need this step.

Use the **Configure Remotes…** entry to add a Google Drive account using
rclone's browser-based OAuth flow.

## Runtime dependency

- rclone

## Current scope

- List configured remotes and directories
- Stream downloads and uploads through KIO
- Create and delete directories and files
- Rename within one remote
- Report remote free space when the backend supports `rclone about`
- Add, reconnect, remove, and open remotes through a small Qt interface

Transfers intentionally use KIO's stream path instead of direct local-file
shortcuts. This lets Dolphin pause and cancel them through normal KIO
backpressure.
