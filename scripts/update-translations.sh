#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 KIO Rclone contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Local replacement for KDE's scripty: regenerates po/kio6_rclone.pot from the
# sources (via Messages.sh) and merges new strings into every existing
# po/<lang>/kio6_rclone.po without losing translations.
#
# Run it after adding or changing user-visible i18n() strings, then fill in the
# empty/fuzzy msgstr entries in the affected catalogs.
#
# Usage: scripts/update-translations.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

for tool in xgettext msgmerge msgfmt; do
    if ! command -v "$tool" >/dev/null; then
        echo "error: $tool not found (install gettext)" >&2
        exit 1
    fi
done

mkdir -p po

# The keyword list KDE's extraction infrastructure passes to xgettext, trimmed
# to the ki18n call families this codebase can use. Messages.sh expands
# $XGETTEXT unquoted, so no value here may contain spaces (same reason KDE
# uses This_file_is_part_of_KDE as its copyright holder).
export XGETTEXT="xgettext \
    --copyright-holder=KIO_Rclone_contributors \
    --msgid-bugs-address=https://github.com/Undead34/kio-rclone/issues \
    --package-name=kio-rclone \
    --from-code=UTF-8 -C --kde -ci18n \
    -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
    -kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3 \
    -kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3 \
    -kI18N_NOOP:1 -kI18NC_NOOP:1c,2"
export podir="$repo_root/po"

./Messages.sh

echo "Regenerated po/kio6_rclone.pot"

shopt -s nullglob
for catalog in po/*/kio6_rclone.po; do
    msgmerge --update --quiet --backup=none "$catalog" po/kio6_rclone.pot
    msgfmt --check --statistics --output-file=/dev/null "$catalog"
done
