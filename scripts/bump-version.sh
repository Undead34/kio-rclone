#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 KIO Rclone contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Bumps the version number everywhere it must be a literal (not derivable at
# build time): CMakeLists.txt, packaging/arch/PKGBUILD, docs/package.json.
#
# What this script does NOT do, on purpose:
#   - Write CHANGELOG.md's new section. That's release notes; only a human
#     (or an assistant told what actually changed) can write those honestly.
#   - Add a <release> entry to app/org.kde.kio-rclone-config.metainfo.xml.
#     Same reason: it needs real prose, not a version string.
#   - Update packaging/arch/PKGBUILD's sha256sums. That hash only exists
#     once the tagged tarball is published on GitHub; compute it after
#     tagging with: curl -sL <tarball-url> | sha256sum
#
# Usage: scripts/bump-version.sh 0.4.0

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <new-version>" >&2
    exit 1
fi

new_version="$1"
if [[ ! "$new_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: version must look like X.Y.Z (got: $new_version)" >&2
    exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

sed -i "s/^project(kio-rclone VERSION [0-9.]\+/project(kio-rclone VERSION $new_version/" CMakeLists.txt
sed -i "s/^pkgver=.*/pkgver=$new_version/" packaging/arch/PKGBUILD
sed -i "s/\"version\": \"[0-9.]\+\"/\"version\": \"$new_version\"/" docs/package.json

echo "Bumped to $new_version in:"
echo "  - CMakeLists.txt"
echo "  - packaging/arch/PKGBUILD (pkgver; sha256sums still needs the real tag hash)"
echo "  - docs/package.json"
echo
echo "Still needs your input before tagging:"
echo "  - CHANGELOG.md: add a \"## $new_version — $(date +%Y-%m-%d)\" section"
echo "  - app/org.kde.kio-rclone-config.metainfo.xml: add a <release version=\"$new_version\"> entry"
echo "  - packaging/arch/.SRCINFO: regenerate with"
echo "      (cd packaging/arch && makepkg --printsrcinfo > .SRCINFO)"
