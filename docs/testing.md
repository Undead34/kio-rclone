# Testing KIO Rclone

This document separates fast, deterministic tests from manual cloud testing.
The automated suite must not need a personal OAuth token or a real remote.

## Automated suite

Configure and run the suite from the repository root:

~~~bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
~~~

Current tests:

| Test | Purpose |
| --- | --- |
| `rcloneurltest` | Parses and builds `rclone:/` URLs. |
| `rclonebackendtest` | Parses rclone JSON and exercises the local rclone backend. |
| `rclonepausetest` | Checks download/upload backpressure with a fake rclone process. |
| `rcloneuploadtest` | Verifies staged publication, exact bytes, cancellation/failure cleanup and preservation of the previous remote file. |
| `rclonedownloadtest` | Verifies unknown-size materialization and exact selection of duplicate remote objects. |
| `appstreamtest` | Validates installed desktop metadata. |

`rclonepausetest` is especially important. It suspends a KIO copy, waits for
the pipeline to settle, verifies that transfer growth stays bounded, then
resumes and checks completion. It also verifies the live upload-status and
finalization messages.

## Package test

The Arch package recipe lives in its own
[AUR repository](https://aur.archlinux.org/packages/kio-rclone) and runs
`ctest` from its `check()` function. From a checkout of that repository:

~~~bash
makepkg -f
~~~

Before installing a package, inspect it:

~~~bash
pacman -Qp ./kio-rclone-*.pkg.tar.zst
pacman -Qlp ./kio-rclone-*.pkg.tar.zst
~~~

## Manual smoke test

Use a dedicated test remote and a disposable cloud folder. Do not test a
release candidate by deleting or renaming files in a personal root directory.

1. Open `rclone:/` in a new Dolphin window.
2. Enter the test remote and list a folder.
3. Create a directory, press F5, then verify that it appears once.
4. Upload a small file and confirm that the notification shows upload status.
5. Download it, pause mid-transfer, confirm network activity settles, then
   resume and compare its checksum.
6. Rename the file within the remote, then remove it.
7. Test an overwrite prompt and a cancel action.
8. Restart Dolphin and verify the remote still opens.
9. Open, edit and save a TXT plus one ODT/DOCX file through `rclone:/`; close
   the application, reopen the file and verify the edited bytes/content.
10. Open a native Google document exported by rclone and verify it has real
    content, a nonzero size and is presented read-only.
11. Create two disposable Drive objects with the same exported name. Verify
    that Dolphin shows one read-only entry and LibreOffice opens one valid
    document rather than concatenated/corrupt bytes.

For Google Drive, perform this with a private OAuth client. A shared rclone
client can introduce quota delays that make a release candidate look slow even
when the worker is correct.

## Manual compatibility matrix

Run this matrix before a public release:

| Area | Minimum coverage |
| --- | --- |
| KDE session | Current Plasma release on Wayland and, when available, X11 |
| Local source | Upload from a local filesystem to a remote |
| Local destination | Download from a remote to a local filesystem |
| Provider | Google Drive with private OAuth; one non-Google rclone backend |
| File size | Small file, multi-chunk file and a filename with spaces/Unicode |
| Document editing | TXT and one ZIP-based office format saved and reopened |
| Drive edge cases | Native export with unknown size and duplicate object names |
| Failure path | Cancel, network interruption and denied/expired OAuth |
| UI | Dolphin notification, F5, context menu, Properties/free-space query |

Record rclone version, Plasma/KF version and the result of every row in the
release issue or milestone.

## Performance checks

Measure provider setup time separately from payload throughput. The important
numbers are:

- time until the first directory entry appears;
- time until the first transfer byte moves;
- sustained upload and download rate;
- delay between the last byte and successful remote confirmation.

Use the same file, network and private OAuth client when comparing versions.
Do not treat a single Google Drive run under a shared quota as a benchmark.

## CI target

The desired continuous-integration gate is:

1. Configure with the supported minimum Qt/KF versions.
2. Build with warnings enabled.
3. Run all CTest tests.
4. Build the Arch package and run `check()`.
5. Upload the package and CTest log as CI artifacts.

Cloud credentials must never be stored in CI. Provider testing belongs in a
separate manually authorized release job with a disposable test account.
