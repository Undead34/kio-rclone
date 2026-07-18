#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 KIO Rclone contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Bumps the version number in the two in-repo literals that are not derivable
# at build time: CMakeLists.txt and docs/package.json.
#
# What this script does NOT do:
#   - Write CHANGELOG.md's new section, or add a <release> entry to
#     app/org.kde.kio-rclone-config.metainfo.xml. Both need real prose.
#   - Bump the Arch package. The PKGBUILD lives in its own AUR repository;
#     bump pkgver and run updpkgsums there after the release tarball exists.
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
sed -i "s/\"version\": \"[0-9.]\+\"/\"version\": \"$new_version\"/" docs/package.json

echo "Bumped to $new_version in:"
echo "  - CMakeLists.txt"
echo "  - docs/package.json"
echo
echo "Still needs your input before tagging:"
echo "  - CHANGELOG.md: add a \"## $new_version — $(date +%Y-%m-%d)\" section"
echo "  - app/org.kde.kio-rclone-config.metainfo.xml: add a <release version=\"$new_version\"> entry"
echo
echo "After the release tarball is published, in the AUR repo:"
echo "  - set pkgver=$new_version, run updpkgsums, regenerate .SRCINFO, push"
