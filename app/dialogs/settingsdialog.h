/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "cache/directorylistingpolicy.h"
#include "cache/directorysnapshotcache.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

/**
 * Transactional editor for Rclone's general settings.
 *
 * Controls only update local dialog state. The persistent policy changes when
 * the user explicitly accepts the dialog; Cancel leaves it untouched.
 */
class SettingsDialog final : public QDialog
{
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    [[nodiscard]] DirectoryListingPolicy selectedPolicy() const;
    void updatePresentation();

    DirectoryListingPolicy m_initialPolicy;
    QComboBox *m_mode = nullptr;
    QSpinBox *m_freshness = nullptr;
    QLabel *m_summary = nullptr;
    QCheckBox *m_clearSavedListings = nullptr;
};
