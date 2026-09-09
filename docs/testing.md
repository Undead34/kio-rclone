---
description: Automated, localization, package, and manual checks required to validate KIO Rclone.
---

# Testing

Automated tests must run without cloud credentials, a personal OAuth client, or
a real remote. Use a dedicated remote and disposable folder for manual checks.

## Automated suite

From the repository root:

~~~bash
cmake -S . -B build/test -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON
cmake --build build/test
ctest --test-dir build/test --output-on-failure
~~~

| Test | Coverage |
| --- | --- |
| `rcloneurltest` | `rclone:/` URL parsing and construction. |
| `rcloneclienttest` | rclone JSON parsing and local client behavior. |
| `rclonelocationtest` | Pure standard and Google Drive public-path to rclone-spec mapping. |
| `rclonepausetest` | Upload/download backpressure and resume behavior. |
| `rcloneuploadtest` | Atomic publication, exact bytes, cancellation, and cleanup. |
| `rclonedownloadtest` | Unknown-size and duplicate-object materialization. |
| `directorysnapshotcachetest` | Snapshot persistence, expiry, policy persistence, configuration invalidation, and private cache permissions. |
| `rclonedirectorycachetest` | Worker cache hits for listings/stat/MIME, remote-only mode, explicit reload bypass, and mutation invalidation. |
| `rclonedrivehubtest` | Drive hub labels, Shared Drive IDs, connection strings, filtered-view write protection, and My Drive writes. |
| `appstreamtest` | Installed AppStream metadata. |
| `dolphin-actions-plugin-metadata` | Native Dolphin action-plugin metadata and MIME registration. |
| `mimefiletest` | Installed shared MIME definition for the launcher. |
| `driveactiontargettest` | Google Drive service-menu URL parsing, `rclone-id` handling, Shared Drive mapping, and browser URL construction. |
| `dolphinactionplugintest` | Native plugin loading plus Drive-only visibility and menu composition. |

## Localization

Validate every changed catalog before review:

~~~bash
msgfmt --check --statistics -o /dev/null po/el/kio6_rclone.po
~~~

This checks catalog syntax and format placeholders, not visual rendering or
translation coverage. Run [the installed-language check](/development#validate-an-installed-language)
whenever a catalog changes.

## Package check

The Arch recipe lives in the separate
[AUR repository](https://aur.archlinux.org/packages/kio-rclone). From that
checkout:

~~~bash
makepkg -f
pacman -Qp ./kio-rclone-*.pkg.tar.zst
pacman -Qlp ./kio-rclone-*.pkg.tar.zst
~~~

`makepkg -f` runs the recipe's `check()` function. Do not publish a package
that fails this step.

## Manual release check

Use a private OAuth client for Google Drive and one non-Google backend. Record
the rclone, Plasma, and KDE Frameworks versions with the result.

1. Open `rclone:/` in a new Dolphin window.
2. Open **Configure Remotes…**. It must open Rclone Remotes directly, without
   a desktop-entry prompt. Press F5 in `rclone:/`; listing the root must not
   open another configuration window.
3. Browse a folder in the test remote, close Dolphin completely, reopen it
   within the configured fresh-cache window, and confirm the listing appears
   promptly. Then select **Always check the remote** in Settings and confirm a
   reopening waits for the remote listing.
4. For Google Drive, confirm the remote root is the hub, **My Drive** opens
   the ordinary root, **Shared Drives** uses stable IDs behind its display
   names, and the filtered views reject writes.
5. In **My Drive**, right-click a disposable file and folder. Confirm that
   **Google Drive** opens/copies the expected browser URL and that **New Google
   Drive files** opens a blank Workspace editor for the selected folder. In
   **Shared With Me**, **Starred**, and **Trash**, verify that items with
   `rclone-id` can still open and copy their Drive URL. On a non-Google
   `rclone:` remote, confirm that no **Google Drive** menu appears at all.
6. Create a directory and refresh it once.
7. Upload a small file, download it, compare its checksum, and verify pause,
   resume, cancellation, and overwrite handling.
8. Rename and delete the disposable file.
9. Restart Dolphin and confirm that the remote remains available.
10. Open a TXT and one ODT/DOCX file, then verify the saved content after
   reopening it. Treat Google-native exports and duplicate remote names as
   read-only cases.
11. If the release changes a translation, run its visual locale check before
   accepting the release.

Never include tokens, client secrets, unredacted configuration, or personal
files in test reports or CI logs.
