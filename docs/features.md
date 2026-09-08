---
description: What Dolphin can do through rclone:/ — browsing, transfers, document editing — and what KIO Rclone does not try to replace.
---

# Features

KIO Rclone makes a remote feel natural in Dolphin while leaving provider-
specific decisions to rclone.

| Dolphin action | Behavior |
| --- | --- |
| Open `rclone:/` | Shows remotes configured in rclone. |
| Enter folders | Lists the provider through rclone. |
| Download | Streams normal files and materializes unknown-size or duplicate-name objects first. |
| Open in LibreOffice or an editor | Uses KIOFuse's full-file cache for local, seekable access. |
| Upload/save | Uploads to a temporary remote name and publishes only after completion. |
| Create a folder | Uses `rclone mkdir`. |
| Rename/move | Uses `rclone moveto`. |
| Delete | Uses `deletefile`, `rmdir`, or `purge` as appropriate. |
| View free space | Uses `rclone about` when supported by the backend. |
| Configure | Opens KIO Rclone's small configurator. |

## Fast folder reopening, with bounded freshness

KIO Rclone keeps a small, private cache of complete successful directory
listings in KDE's cache location (normally `~/.cache/kio-rclone/`). It uses
KDE's shared cache facility, so a listing can still be reused after Dolphin and
its worker process have exited. It is a short-lived navigation optimization,
not an offline filesystem.

The default **Fresh cache** policy reuses a listing for 15 seconds. This makes
folders visited just before closing Dolphin open without another provider
round-trip. The configurator can change that window from 1 to 60 seconds, or
select **Strict** to always ask rclone and the provider instead.

- Only complete, successful listings are stored. Errors and cancelled listings
  are never cached.
- The cache is bounded (8 MiB), evicts least-recently-used snapshots, contains
  no rclone credentials, and is invalidated automatically when the rclone
  configuration changes.
- Downloads and every mutable operation still resolve their target remotely.
  A successful upload, create, rename, or delete clears snapshots and sends a
  KIO directory-change notification to open views.
- A KIO caller may request `cache=reload` or `cache=refresh` to bypass the
  snapshot. With a normal fresh listing there can still be up to the selected
  freshness window of externally changed data; use **Strict** when that is not
  acceptable.

There is deliberately no background tree walk, `ListR`, or cache-first
correction pass in this path. Those would add network activity and surprising
view changes without solving the common close-and-reopen delay as directly.

## What makes transfers special

Transfers between locations pass through KIO, preserving Dolphin's controls:

- Pause stops requesting data and feeding rclone.
- Cancel terminates the transfer process.
- A failed or cancelled upload never replaces the remote file with partial data.
- Uploads receive rclone JSON statistics for percentage, speed, and ETA.
- **Finalizing upload…** distinguishes 100% transferred from provider confirmation.

See [Transfers](/transfers) for details.

<!--
## Documents, editing, and Google Drive

Ordinary files such as TXT, ODT, DOCX, XLSX, and PPTX open through KIOFuse's
local full-file cache. When saved, KIO Rclone validates the complete file and
publishes the new version at the end.

There are two read-only exceptions:

- Native Google documents exported by rclone do not have a stable size until
  downloaded. They open at the correct size, but are not automatically
  re-imported to avoid replacing a collaborative document.
- If a folder contains several objects with the same name, the newest is
  shown and downloaded by ID when the backend supports it. The path remains
  read-only until the duplicates are resolved.
-->

## What it does not aim to replace

| Need | Use |
| --- | --- |
| Synchronize directories | `rclone sync` or `rclone bisync` |
| Mount a remote as a filesystem | `rclone mount` |
| Queue/retry outside Dolphin | A dedicated rclone workflow |
| Offline/VFS cache | `rclone mount` with suitable VFS options |
| Provider-exclusive features | The corresponding rclone command/backend |

This separation is deliberate: the worker stays small while rclone keeps the
provider, OAuth, and retry knowledge.
