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
- A KIO caller may request `cache=reload` or `cache=refresh` to discard
  snapshots and force a remote listing. With a normal fresh listing there can
  still be up to the selected freshness window of externally changed data; use
  **Strict** when that is not acceptable.

There is deliberately no background tree walk, `ListR`, or cache-first
correction pass in this path. Those would add network activity and surprising
view changes without solving the common close-and-reopen delay as directly.

## Google Drive is a small hub, not one flat root

For a Google Drive remote, its root in Dolphin is organized into **My Drive**,
**Shared With Me**, **Shared Drives**, **Trash**, and **Starred**. That keeps
the main tree predictable and delegates each provider-specific query to rclone.
My Drive and a selected Shared Drive can be changed when the provider permits
it; the filtered views are deliberately read-only. Known Drive folder IDs can
also be opened through the location bar without a second account database.

See [Google Drive and GCP](/google-drive#drive-views-in-dolphin) for the exact
paths and the intentionally conservative limits.

## Google Drive actions in Dolphin

Right-clicking a Google Drive item adds native Dolphin menus. In **My Drive**
and an individual **Shared Drive**, it also adds a folder-only creation menu:

- **Google Drive** opens the selected item in Drive, copies its Drive link, or
  opens its containing folder in the browser.
- **New Google Drive files** creates a blank Google Doc, Sheet, Slide, or
  Drawing in the selected folder.

The helper uses the namespaced `rclone-id` in the KIO item's `UDS_URL` when it
is present (`rclone-orig-id` is a safe fallback), so opening and copying work
in **Shared With Me**, **Starred**, and **Trash** too, without a Google API
request. Workspace creation and a path-based fallback remain limited to My
Drive and individual Shared Drives. Older physical-view URLs retain a
read-only rclone metadata fallback; neither path uses `rclone link` or changes
an item's sharing settings. The browser uses its existing Google session and
KIO Rclone never reads browser cookies or OAuth tokens for these actions.

The menus come from a category-2 native Dolphin action plugin. Before it adds
anything to a contextual menu, it checks the selected URL for exactly one
`rclone-remote-type=drive` query item. Non-Drive rclone remotes receive no
Google Drive menu at all. This visibility decision is local URL parsing only:
it does not launch rclone or make a network request. The action helper still
verifies the configured remote when an action is invoked, so a stale or forged
URL cannot make it operate on a non-Drive remote.

**Offline access** is not offered here: its equivalent in the reference
FUSE-based workflow only warms an rclone mount's VFS cache. `rclone:/` is a
KIO protocol rather than a persistent offline mount; use `rclone mount` when
that is required.

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
