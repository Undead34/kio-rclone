#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build, test, remove the previous manual system install, and install the
# current checkout. This is intended for the fast system-integration debug
# loop described in docs/development.md.
#
# Usage: scripts/rebuild-system-debug.sh
#
# The build is kept in build/system-debug and is installed into /usr. Set
# KIO_RCLONE_BUILD_DIR, KIO_RCLONE_BUILD_TYPE, or KIO_RCLONE_INSTALL_PREFIX to
# override those defaults when needed.

set -Eeuo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="${KIO_RCLONE_BUILD_DIR:-build/system-debug}"
build_type="${KIO_RCLONE_BUILD_TYPE:-Debug}"
install_prefix="${KIO_RCLONE_INSTALL_PREFIX:-/usr}"
manifest="$build_dir/install_manifest.txt"

if [[ "$install_prefix" != /* || "$install_prefix" == "/" ]]; then
    echo "error: KIO_RCLONE_INSTALL_PREFIX must be an absolute non-root path (got: $install_prefix)" >&2
    exit 1
fi

if (( EUID == 0 )); then
    echo 'error: run this script as your desktop user, not through sudo.' >&2
    exit 1
fi

for tool in cmake ctest ninja sudo kbuildsycoca6; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required command not found: $tool" >&2
        exit 1
    fi
done

echo "Configuring $build_type build in $build_dir"
cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX="$install_prefix"

echo 'Building'
cmake --build "$build_dir"

echo 'Running tests'
ctest --test-dir "$build_dir" --output-on-failure

if [[ -s "$manifest" ]]; then
    # Never use a manifest from another prefix: it could remove unrelated
    # files when this script is pointed at a different install location.
    prefix_with_slash="${install_prefix%/}/"
    if ! awk -v prefix="$prefix_with_slash" '
        NF && index($0, prefix) != 1 {
            printf "error: install manifest contains a path outside %s: %s\n",
                prefix, $0 > "/dev/stderr"
            invalid = 1
        }
        END { exit invalid }
    ' "$manifest"; then
        exit 1
    fi

    echo "Removing previous install listed in $manifest"
    # CMake writes one path per line. -d '\n' also keeps paths containing
    # spaces intact (the development guide's xargs command assumes none).
    sudo xargs -r -d '\n' rm -vf -- < "$manifest"
else
    echo "No previous install manifest at $manifest"
fi

echo "Installing into $install_prefix"
# With the defaults this is the exact command from the development guide:
# sudo cmake --install build/system-debug
sudo cmake --install "$build_dir"

# KDE's cache update must run as the desktop user, not through sudo.
kbuildsycoca6 --noincremental

echo 'System debug rebuild/install complete.'
