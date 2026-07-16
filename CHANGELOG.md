# Changelog

KIO Rclone follows semantic versioning. Patch releases contain compatible bug
fixes, minor releases add compatible behavior, and a major release may change
user-visible protocol or packaging behavior.

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
