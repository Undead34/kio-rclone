#!/bin/sh
#
# SPDX-FileCopyrightText: 2026 KIO Rclone contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# KDE-standard message extraction entry point. KDE's l10n infrastructure
# (scripty) provides $XGETTEXT and $podir; outside that infrastructure, run
# scripts/update-translations.sh, which sets both and then merges the result
# into the existing po/*/ catalogs.
$XGETTEXT `find src config -name \*.cpp -o -name \*.h` -o $podir/kio6_rclone.pot
