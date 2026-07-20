---
description: Install KIO Rclone, configure a remote, and open rclone:/ in Dolphin.
---

# Getting started

KIO Rclone uses exactly the remotes that rclone already knows. First make sure
rclone can list the remote from a terminal, then open it in Dolphin.

## 1. Install the package

On Arch Linux, install [`kio-rclone`](https://aur.archlinux.org/packages/kio-rclone)
from the AUR:

~~~bash
yay -S kio-rclone
kbuildsycoca6 --noincremental
~~~

If you already had a version installed, `pacman` replaces it; you do not need
to uninstall it first. To build from source instead, see the
[README](https://github.com/Undead34/kio-rclone#installation).

## 2. Configure a remote

Open **Rclone Remotes** from the application menu or run:

~~~bash
kio-rclone-config
~~~

You can add Google Drive, reconnect an existing remote, remove only its
configuration entry, open it directly in Dolphin, or use **Other Providers…**
for rclone's regular configurator.

> [!TIP]
> Test the remote in rclone before debugging Dolphin:
>
> ~~~bash
> rclone lsd 'My remote:'
> ~~~

## 3. Open it in Dolphin

Enter `rclone:/` in Dolphin's location bar. You will see every configured
remote. A remote named `Google Drive` is available at:

~~~text
rclone:/Google%20Drive/
~~~

Dolphin normally displays spaces for you; `%20` only appears in a URL typed by
hand.

## 4. Make your first copy

1. Enter a folder in your remote.
2. Drag a local file there, or copy and paste it.
3. Watch Dolphin's notification: an upload shows preparation, remote progress,
   and final confirmation.
4. When it finishes, press F5 to request a fresh listing from the provider.

To understand what happens when you pause a copy, read
[Transfers](/transfers).

## Google Drive: do it right from the start

rclone's shared Client ID has a global quota and can introduce delays or
limits. For a stable experience, configure your own Google Drive OAuth client
before moving many files:

[Create your Google Cloud project](/google-drive)
