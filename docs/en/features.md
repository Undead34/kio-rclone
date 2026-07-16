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

## What makes transfers special

Transfers between locations pass through KIO, preserving Dolphin's controls:

- Pause stops requesting data and feeding rclone.
- Cancel terminates the transfer process.
- A failed or cancelled upload never replaces the remote file with partial data.
- Uploads receive rclone JSON statistics for percentage, speed, and ETA.
- **Finalizing upload…** distinguishes 100% transferred from provider confirmation.

See [Transfers](/en/transfers) for details.

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
