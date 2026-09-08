/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "directorysnapshotcache.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

/**
 * Small, transactional editor for the directory-listing cache policy.
 *
 * Controls only update local dialog state. The persistent policy changes when
 * the user explicitly accepts the dialog; Cancel leaves it untouched.
 */
class DirectoryListingSettingsDialog final : public QDialog
{
public:
    explicit DirectoryListingSettingsDialog(QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    [[nodiscard]] DirectoryCachePolicy selectedPolicy() const;
    void updatePresentation();

    DirectoryCachePolicy m_initialPolicy;
    QComboBox *m_mode = nullptr;
    QSpinBox *m_freshness = nullptr;
    QLabel *m_summary = nullptr;
    QCheckBox *m_clearSavedListings = nullptr;
};
