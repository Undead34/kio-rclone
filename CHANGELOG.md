# Changelog

KIO Rclone follows semantic versioning. Patch releases contain compatible bug
fixes, minor releases add compatible behavior, and a major release may change
user-visible protocol or packaging behavior.

## 0.4.2 — 2026-07-20

### Added

- Provider-specific icons for rclone remotes in `rclone:/`: Google Drive,
  Dropbox, OneDrive and WebDAV get their themed Breeze places icon, resolved
  from a single `rclone config dump` and shared between directory listing and
  `stat`. Other providers keep the generic cloud folder. The root and the
  network entry ship a dedicated icon instead of a generic one.

### Changed

- Reworded the configuration window's description and empty-state hint,
  broadened the launcher's keywords, and dropped the Settings category from
  its desktop entry.
- The documentation site gives every page its own title, description and
  Open Graph/Twitter tags instead of sharing the homepage's, and ships a
  `robots.txt` pointing crawlers at the sitemap.

## 0.4.1 — 2026-07-18

### Changed

- The "Configure Remotes…" entry is now a shortcut file that points at the
  installed application's `.desktop` launcher (via `UDS_TARGET_URL`) instead of
  a pseudo-directory the worker "entered" by spawning the process itself.
  Listing `rclone:/` no longer launches any UI, so recursive walks (Baloo,
  `find`, kio-fuse) cannot trigger it. This removes the lock-file, launch-stamp
  and cooldown guard added in 0.4.0; a single window is now guaranteed solely by
  the application's `KDBusService::Unique` registration, and repeated launches
  activate the existing window (with correct Wayland/X11 focus handling).

## 0.4.0 — 2026-07-18

### Added

- **Add Remote** now covers OneDrive and Dropbox alongside Google Drive. The
  provider form is generated from `rclone config providers`, so its fields
  always match what the installed rclone accepts, and OneDrive gets a guided
  drive-selection step after authorization.
- **Edit** dialog for existing remotes: rename them and switch their OAuth
  client without leaving the application.
- Encrypted rclone configurations are supported: a single password prompt
  unlocks the configuration for the rest of the session.
- OAuth clients already configured in KDE System Settings can be imported into
  a new or existing remote with one click.
- Spanish translation of the KIO worker and the configuration application,
  with standard KDE translation infrastructure (`kio6_rclone` domain, `po/`
  catalogs).

### Fixed

- Duplicate configuration windows could still open when the application was
  launched without a reachable D-Bus session bus (for example from a recursive
  walk of a kio-fuse mount). A lock-file instance guard and a short launch
  cooldown now cover that path.

## 0.3.1 — 2026-07-17

### Fixed

- `kio-rclone-config` could open unbounded duplicate windows: any repeated
  `listDir()` call on the "Configure Remotes…" KIO entry (double clicks,
  indexers, or a recursive tool walking a kio-fuse mount of `rclone:/`)
  launched a brand new process with no single-instance guard. The utility now
  registers as a unique D-Bus application; a second launch attempt raises the
  existing window instead of opening another one.
- The configuration window and its dialogs now use the project's own icon
  instead of the generic `folder-cloud` icon.

## 0.3.0 — 2026-07-16

### Fixed

- Materialize Google-native exports whose rclone size is unknown before
  reporting metadata or opening them, so office applications receive the real
  file size and complete bytes.
- Detect duplicate remote names, expose one deterministic newest entry and use
  the provider object ID when available instead of concatenating every match.
- Keep ambiguous paths and native Google exports read-only to avoid replacing
  the wrong object or destructively reimporting a collaborative document.
- Upload to a unique remote staging name and publish it only after the complete
  transfer succeeds; cancellation and transfer failure preserve the previous
  remote file.
- Validate KIO's advertised source size and refuse to overwrite a destination
  that changed during the upload.

### Changed

- Document editing uses KIOFuse's whole-file cache. Direct KIO FileJob random
  I/O remains disabled until it can be implemented without downloading and
  uploading the full object for every operation.
- MIME type is now declared as part of directory listings.

## 0.2.0 — 2026-07-10

- Added live rclone upload progress and finalization status.
- Streamed only the requested remote file during normal downloads.
- Added private Google OAuth client configuration.
