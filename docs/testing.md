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
| `rclonebackendtest` | rclone JSON parsing and local backend behavior. |
| `rclonepausetest` | Upload/download backpressure and resume behavior. |
| `rcloneuploadtest` | Atomic publication, exact bytes, cancellation, and cleanup. |
| `rclonedownloadtest` | Unknown-size and duplicate-object materialization. |
| `appstreamtest` | Installed AppStream metadata. |
| `desktopfiletest` | Absolute launcher path plus MIME and URI-handler registration. |
| `mimefiletest` | Installed shared MIME definition for the launcher. |

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
3. Browse the test remote; create a directory and refresh it once.
4. Upload a small file, download it, compare its checksum, and verify pause,
   resume, cancellation, and overwrite handling.
5. Rename and delete the disposable file.
6. Restart Dolphin and confirm that the remote remains available.
7. Open a TXT and one ODT/DOCX file, then verify the saved content after
   reopening it. Treat Google-native exports and duplicate remote names as
   read-only cases.
8. If the release changes a translation, run its visual locale check before
   accepting the release.

Never include tokens, client secrets, unredacted configuration, or personal
files in test reports or CI logs.
