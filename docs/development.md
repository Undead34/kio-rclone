---
description: Build, install, remove, test, document, translate, and troubleshoot KIO Rclone from a local checkout.
---

# Development guide

This is the contributor guide. Use separate build directories for each build
type and install target. Do not build in the source tree.

## Prerequisites

On Arch Linux, install the build and validation tools:

~~~bash
sudo pacman -S --needed \
  base-devel cmake extra-cmake-modules ninja pkgconf \
  qt6-base kcoreaddons kdbusaddons ki18n kio kwindowsystem \
  rclone shared-mime-info appstream gettext
~~~

`gettext` is required only for translation work. Node.js and pnpm are required
only for the documentation site. The AUR recipe remains the authoritative list
of package dependencies for distribution builds.

## Build modes

Use `Debug` while changing code. Use `RelWithDebInfo` for a release-like build
with debugging symbols. The recommended layout keeps their outputs and local
installs separate:

| Purpose | CMake build type | Build directory | User prefix |
| --- | --- | --- | --- |
| Debug development | `Debug` | `build/user-debug` | `$HOME/.local/kio-rclone-debug` |
| Release validation | `RelWithDebInfo` | `build/user-release` | `$HOME/.local/kio-rclone-release` |
| System debug integration | `Debug` | `build/system-debug` | `/usr` |
| System release integration | `RelWithDebInfo` | `build/system-release` | `/usr` |

The two user prefixes avoid accidentally uninstalling one mode with the other
mode's install manifest. Only one local prefix should be active in the desktop
session at a time. Both system modes use `/usr`, so only the most recently
installed system mode may be removed with its manifest.

## Build and test without installing

### Debug

~~~bash
cmake -S . -B build/user-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build/user-debug
ctest --test-dir build/user-debug --output-on-failure
~~~

### Release-like

~~~bash
cmake -S . -B build/user-release -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON
cmake --build build/user-release
ctest --test-dir build/user-release --output-on-failure
~~~

Run one test while iterating:

~~~bash
ctest --test-dir build/user-debug --output-on-failure -R '^rcloneurltest$'
~~~

## Install without sudo

Use a user prefix for local integration testing. It does not replace files
owned by pacman and it can be removed using the exact install manifest.

### Debug user prefix

~~~bash
KIO_RCLONE_PREFIX="$HOME/.local/kio-rclone-debug"

cmake -S . -B build/user-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX="$KIO_RCLONE_PREFIX"
cmake --build build/user-debug
ctest --test-dir build/user-debug --output-on-failure
cmake --install build/user-debug
source build/user-debug/prefix.sh
kbuildsycoca6 --noincremental
~~~

### Release-like user prefix

~~~bash
KIO_RCLONE_PREFIX="$HOME/.local/kio-rclone-release"

cmake -S . -B build/user-release -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX="$KIO_RCLONE_PREFIX"
cmake --build build/user-release
ctest --test-dir build/user-release --output-on-failure
cmake --install build/user-release
source build/user-release/prefix.sh
kbuildsycoca6 --noincremental
~~~

`prefix.sh` activates the prefix only in the current terminal. It is enough to
run `kio-rclone-config` there. Dolphin needs the same plugin and data paths in
its Plasma session. Create one file at
`~/.config/environment.d/kio-rclone.conf` for the selected prefix:

~~~text
QT_PLUGIN_PATH=${HOME}/.local/kio-rclone-debug/lib/plugins:${QT_PLUGIN_PATH}
XDG_DATA_DIRS=${HOME}/.local/kio-rclone-debug/share:${XDG_DATA_DIRS}
PATH=${HOME}/.local/kio-rclone-debug/bin:${PATH}
~~~

For the release-like prefix, replace `kio-rclone-debug` with
`kio-rclone-release`. Log out and back in after changing this file, then run
`kbuildsycoca6 --noincremental` once in the new session.

### Remove a user-prefix install

Close Dolphin and Rclone Remotes. Use the install manifest from the same build
directory and prefix that performed the installation:

~~~bash
test -s build/user-debug/install_manifest.txt || {
  echo 'Missing user-debug install manifest.' >&2
  exit 1
}
sed -n '1,200p' build/user-debug/install_manifest.txt
xargs -r rm -v < build/user-debug/install_manifest.txt
rm -f ~/.config/environment.d/kio-rclone.conf
kbuildsycoca6 --noincremental
~~~

### Remove a release-like user-prefix install

~~~bash
test -s build/user-release/install_manifest.txt || {
  echo 'Missing user-release install manifest.' >&2
  exit 1
}
sed -n '1,200p' build/user-release/install_manifest.txt
xargs -r rm -v < build/user-release/install_manifest.txt
rm -f ~/.config/environment.d/kio-rclone.conf
kbuildsycoca6 --noincremental
~~~

Do not reuse a manifest after changing `CMAKE_INSTALL_PREFIX`; it only
describes the files written by the configuration that created it. A logout/login
removes the prefix paths from the graphical session.

## Install with sudo into `/usr`

Use this only for system integration testing. A manual `/usr` install conflicts
with the AUR package because pacman does not own files installed by CMake. Do
not install the AUR package while either system prefix is present.

### Debug system install

~~~bash
cmake -S . -B build/system-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/system-debug
ctest --test-dir build/system-debug --output-on-failure
sudo cmake --install build/system-debug
kbuildsycoca6 --noincremental
~~~

### Release-like system install

~~~bash
cmake -S . -B build/system-release -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/system-release
ctest --test-dir build/system-release --output-on-failure
sudo cmake --install build/system-release
kbuildsycoca6 --noincremental
~~~

Run `kbuildsycoca6` as the desktop user, not through `sudo`.

### Remove a system install

Use the matching install manifest and inspect it before deletion:

~~~bash
test -s build/system-debug/install_manifest.txt || {
  echo 'Missing system-debug install manifest.' >&2
  exit 1
}
sed -n '1,200p' build/system-debug/install_manifest.txt
sudo xargs -r rm -v < build/system-debug/install_manifest.txt
kbuildsycoca6 --noincremental
~~~

### Remove a release-like system install

~~~bash
test -s build/system-release/install_manifest.txt || {
  echo 'Missing system-release install manifest.' >&2
  exit 1
}
sed -n '1,200p' build/system-release/install_manifest.txt
sudo xargs -r rm -v < build/system-release/install_manifest.txt
kbuildsycoca6 --noincremental
~~~

If a manifest no longer exists, do not guess `/usr` paths or remove directories
recursively. Restore package ownership with the package that provides the
files, then manage it through pacman.

## Tests and documentation

The complete validation matrix is in [Testing](/testing). The fast loop is:

~~~bash
cmake --build build/user-debug
ctest --test-dir build/user-debug --output-on-failure
git diff --check
~~~

Build the documentation site from the repository root:

~~~bash
pnpm --dir docs install --frozen-lockfile
pnpm --dir docs docs:dev
~~~

Use `pnpm --dir docs docs:build` for the production build.

## Add or update a language

Application strings use KDE gettext catalogs in `po/<locale>/kio6_rclone.po`.
`ki18n_install(po)` discovers every catalog below `po/`; a new language needs
no CMake registration.

### Add a catalog

First regenerate the template, then initialize the locale:

~~~bash
scripts/update-translations.sh

KIO_RCLONE_LOCALE=el
mkdir -p "po/$KIO_RCLONE_LOCALE"
msginit --locale="$KIO_RCLONE_LOCALE" \
  --input=po/kio6_rclone.pot \
  --output-file="po/$KIO_RCLONE_LOCALE/kio6_rclone.po"
~~~

Translate `msgstr` values without changing `msgid`, message contexts, or
`kde-format` markers. After a source string changes, run
`scripts/update-translations.sh` again; it merges the new template into every
existing catalog.

### Validate a catalog

~~~bash
KIO_RCLONE_LOCALE=el
msgfmt --check --statistics --output-file=/dev/null \
  "po/$KIO_RCLONE_LOCALE/kio6_rclone.po"
if msgattrib --untranslated --no-obsolete \
  "po/$KIO_RCLONE_LOCALE/kio6_rclone.po" | grep -q '^msgid '; then
  echo "Untranslated $KIO_RCLONE_LOCALE messages remain." >&2
  exit 1
fi
~~~

`msgfmt` validates syntax and format placeholders; the second command rejects
untranslated non-obsolete messages. Build and install the selected mode before
the visual check.

Desktop-entry and AppStream translations are separate metadata. Add their
locale-specific fields deliberately when the translated application name or
software-center description is in scope. Website translations also require a
matching `docs/<locale>/` tree and VitePress locale configuration.

### Validate an installed language

For a focused application check, you do not need to add a locale to the whole
system. KDE's localization layer accepts `LANGUAGE`; use the catalog locale
(`el` here) while keeping the character type on an available UTF-8 locale.
Unset `LC_ALL`, because it would override `LANGUAGE`:

~~~bash
KIO_RCLONE_LOCALE=el
env -u LC_ALL LANGUAGE="$KIO_RCLONE_LOCALE" LC_CTYPE=C.UTF-8 \
  kio-rclone-config
~~~

Replace `el` with the locale directory of the catalog you are testing. This
launch method is also the recommended visual check after adding a language; it
does not modify the system locale configuration.

Close every existing **Rclone Remotes** window before launching: the
application is single-instance and otherwise activates the window that started
in a different locale. This check selects the installed `el` catalog without
changing the system's default language. A full `el_GR.UTF-8` locale is only
needed when testing locale-specific formatting or a complete desktop session;
on Arch, enable `el_GR.UTF-8 UTF-8` in `/etc/locale.gen` and run
`sudo locale-gen` if that broader test is required.

For a package or `/usr` install:

~~~bash
env -u LC_ALL LANGUAGE=el LC_CTYPE=C.UTF-8 kio-rclone-config
~~~

For the debug user prefix:

~~~bash
KIO_RCLONE_PREFIX="$HOME/.local/kio-rclone-debug"
KIO_RCLONE_LOCALE=el
env -u LC_ALL LANGUAGE="$KIO_RCLONE_LOCALE" LC_CTYPE=C.UTF-8 \
XDG_DATA_DIRS="$KIO_RCLONE_PREFIX/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}" \
"$KIO_RCLONE_PREFIX/bin/kio-rclone-config"
~~~

Check the title, buttons, headings, empty state, error messages, and
ellipsis/accelerator rendering. To validate worker strings in Dolphin, use a
fresh Plasma/Dolphin session in that locale; an existing Dolphin process keeps
the locale with which it started.

## Development problems

| Symptom | Action |
| --- | --- |
| CMake keeps an old prefix or build type | Use the build directory assigned to that mode, or create a new named build directory. |
| `rclone:/` does not appear after a user install | Verify the selected prefix in `~/.config/environment.d/kio-rclone.conf`, log in again, run `kbuildsycoca6 --noincremental`, then restart Dolphin. |
| The local translation stays in English | Confirm the installed `.mo` exists under `<prefix>/share/locale`, start a new configuration window with `LANG`, and verify `XDG_DATA_DIRS` for a user prefix. |
| The AUR package reports conflicting `/usr` files | Remove the manual system install through its manifest before installing the package. |
| A catalog fails to build | Run `msgfmt --check`; fix the reported plural, placeholder, or markup issue before running the full build. |

Use [Troubleshooting](/troubleshooting) for rclone, OAuth, transfer, and
Dolphin behavior after the application is installed.

## Before opening a pull request

1. Run the full CTest suite for the affected build mode.
2. Run `git diff --check`.
3. Build the documentation after Markdown or VitePress changes.
4. Regenerate and validate catalogs after user-visible string changes.
5. Perform the manual checks that apply to the change.
6. Use [Releasing](/releasing) only for a public version.
