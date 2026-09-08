/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "settingsdialog.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_initialPolicy(DirectorySnapshotCache::policy())
{
    setWindowTitle(i18n("Settings"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("configure")));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto *heading = new QLabel(i18n("<h2>Directory listing cache</h2>"), this);
    layout->addWidget(heading);

    auto *description = new QLabel(i18n("Choose how folders you have already visited are opened in Dolphin."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_mode = new QComboBox(this);
    m_mode->addItem(i18n("Use recent listings (recommended)"), static_cast<int>(DirectoryCacheMode::Fresh));
    m_mode->addItem(i18n("Always check the remote"), static_cast<int>(DirectoryCacheMode::Strict));
    m_mode->setCurrentIndex(m_initialPolicy.mode == DirectoryCacheMode::Strict ? 1 : 0);
    form->addRow(i18n("When opening a folder:"), m_mode);

    m_freshness = new QSpinBox(this);
    m_freshness->setRange(DirectorySnapshotCache::MinimumFreshnessSeconds, DirectorySnapshotCache::MaximumFreshnessSeconds);
    m_freshness->setSingleStep(5);
    m_freshness->setValue(m_initialPolicy.freshnessSeconds);
    form->addRow(i18n("Reuse a listing for:"), m_freshness);
    layout->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    m_clearSavedListings = new QCheckBox(i18n("Clear saved listings when saving"), this);
    layout->addWidget(m_clearSavedListings);

    auto *buttons = new QDialogButtonBox(Qt::Horizontal, this);
    auto *saveButton = buttons->addButton(i18n("Save"), QDialogButtonBox::AcceptRole);
    saveButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    saveButton->setDefault(true);
    auto *cancelButton = buttons->addButton(i18n("Cancel"), QDialogButtonBox::RejectRole);
    cancelButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
    layout->addWidget(buttons);

    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updatePresentation();
    });
    connect(m_freshness, qOverload<int>(&QSpinBox::valueChanged), this, [this](int seconds) {
        m_freshness->setSuffix(i18np(" second", " seconds", seconds));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    m_freshness->setSuffix(i18np(" second", " seconds", m_freshness->value()));
    updatePresentation();
}

DirectoryCachePolicy SettingsDialog::selectedPolicy() const
{
    DirectoryCachePolicy policy;
    policy.mode = static_cast<DirectoryCacheMode>(m_mode->currentData().toInt());
    policy.freshnessSeconds = m_freshness->value();
    return policy;
}

void SettingsDialog::updatePresentation()
{
    const bool usesFreshCache = static_cast<DirectoryCacheMode>(m_mode->currentData().toInt()) == DirectoryCacheMode::Fresh;
    m_freshness->setEnabled(usesFreshCache);
    m_summary->setText(usesFreshCache
                           ? i18n("Recently visited folders can open faster. File-changing actions still check the remote.")
                           : i18n("Every directory opening checks rclone and the remote provider."));
}

void SettingsDialog::accept()
{
    const DirectoryCachePolicy policy = selectedPolicy();
    const bool policyChanged = policy.mode != m_initialPolicy.mode || policy.freshnessSeconds != m_initialPolicy.freshnessSeconds;
    if (policyChanged) {
        DirectorySnapshotCache::setPolicy(policy);
    } else if (m_clearSavedListings->isChecked()) {
        DirectorySnapshotCache::clearPersistent();
    }

    QDialog::accept();
}
