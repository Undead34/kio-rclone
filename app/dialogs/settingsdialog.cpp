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
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QPushButton>

namespace
{

QLabel *createDescriptionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QFrame *createSeparator(QWidget *parent)
{
    auto *separator = new QFrame(parent);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    return separator;
}

}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_initialPolicy(DirectoryListingPolicyStore::load())
{
    setWindowTitle(i18n("Rclone Settings"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("configure")));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    /*
     * Keep the introduction short. The window title already provides
     * the "Settings" context, so there is no need for another large heading.
     */
    auto *description = createDescriptionLabel(
        i18n("Configure how Dolphin browses Rclone remotes."),
        this);
    layout->addWidget(description);

    /*
     * Folder listings
     *
     * All controls related to listing caching belong to one visual group.
     */
    auto *listingGroup = new QGroupBox(i18n("Folder listings"), this);
    auto *listingLayout = new QVBoxLayout(listingGroup);
    listingLayout->setSpacing(10);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);

    /*
     * Listing mode
     */
    m_mode = new QComboBox(listingGroup);

    m_mode->addItem(
        i18n("Use recent listings (recommended)"),
        static_cast<int>(DirectoryListingMode::RecentSnapshot));

    m_mode->addItem(
        i18n("Always check the remote"),
        static_cast<int>(DirectoryListingMode::RemoteOnly));

    m_mode->setCurrentIndex(
        m_initialPolicy.mode == DirectoryListingMode::RemoteOnly ? 1 : 0);

    const QString modeHelp = i18n(
        "Choose whether Dolphin may reuse a recently saved folder listing "
        "or requests a new listing from the remote each time.");

    m_mode->setToolTip(modeHelp);
    m_mode->setAccessibleName(i18n("How to open folders"));
    m_mode->setAccessibleDescription(modeHelp);

    form->addRow(i18n("How to open folders:"), m_mode);

    /*
     * Listing freshness
     */
    m_freshness = new QSpinBox(listingGroup);

    m_freshness->setRange(
        DirectoryListingPolicy::MinimumFreshnessSeconds,
        DirectoryListingPolicy::MaximumFreshnessSeconds);

    m_freshness->setSingleStep(5);
    m_freshness->setValue(m_initialPolicy.freshnessSeconds);

    m_freshness->setSuffix(
        i18np(" second", " seconds", m_freshness->value()));

    const QString freshnessHelp = i18n(
        "How long Dolphin may reuse a saved folder listing before requesting "
        "a new one from the remote.");

    m_freshness->setToolTip(freshnessHelp);
    m_freshness->setAccessibleName(i18n("Keep listings for"));
    m_freshness->setAccessibleDescription(freshnessHelp);

    form->addRow(i18n("Keep listings for:"), m_freshness);

    listingLayout->addLayout(form);

    /*
     * Dynamic explanation.
     *
     * This is visible instead of hiding important behavior inside a tooltip.
     */
    m_summary = createDescriptionLabel(QString(), listingGroup);
    listingLayout->addWidget(m_summary);

    /*
     * Cached-listing maintenance belongs to the same feature but is
     * visually separated because it is an explicit cleanup action.
     */
    listingLayout->addWidget(createSeparator(listingGroup));

    auto *savedListingsLabel = new QLabel(i18n("Saved listings"), listingGroup);
    listingLayout->addWidget(savedListingsLabel);

    m_clearSavedListings = new QCheckBox(
        i18n("Clear saved listings when saving"),
        listingGroup);

    const QString clearHelp = i18n(
        "Removes locally saved folder listings when these settings are saved. "
        "Remote files and folders are not affected.");

    m_clearSavedListings->setToolTip(clearHelp);
    m_clearSavedListings->setAccessibleName(
        i18n("Clear saved listings when saving"));
    m_clearSavedListings->setAccessibleDescription(clearHelp);

    listingLayout->addWidget(m_clearSavedListings);

    auto *clearDescription = createDescriptionLabel(
        i18n("Useful when a folder appears outdated. "
             "Remote files are never deleted."),
        listingGroup);

    listingLayout->addWidget(clearDescription);

    layout->addWidget(listingGroup);

    /*
     * Keep the actions at the bottom, separated naturally by available space.
     */
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        Qt::Horizontal,
        this);

    if (auto *saveButton = buttons->button(QDialogButtonBox::Save)) {
        saveButton->setIcon(
            QIcon::fromTheme(QStringLiteral("document-save")));
        saveButton->setDefault(true);
    }

    if (auto *cancelButton = buttons->button(QDialogButtonBox::Cancel)) {
        cancelButton->setIcon(
            QIcon::fromTheme(QStringLiteral("dialog-cancel")));
    }

    layout->addWidget(buttons);

    connect(
        m_mode,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int) {
            updatePresentation();
        });

    connect(
        m_freshness,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        [this](int seconds) {
            m_freshness->setSuffix(
                i18np(" second", " seconds", seconds));
        });

    connect(
        buttons,
        &QDialogButtonBox::accepted,
        this,
        &SettingsDialog::accept);

    connect(
        buttons,
        &QDialogButtonBox::rejected,
        this,
        &SettingsDialog::reject);

    updatePresentation();
}

DirectoryListingPolicy SettingsDialog::selectedPolicy() const
{
    DirectoryListingPolicy policy;

    policy.mode =
        static_cast<DirectoryListingMode>(
            m_mode->currentData().toInt());

    policy.freshnessSeconds =
        m_freshness->value();

    return policy;
}

void SettingsDialog::updatePresentation()
{
    const bool usesFreshCache =
        static_cast<DirectoryListingMode>(
            m_mode->currentData().toInt())
        == DirectoryListingMode::RecentSnapshot;

    m_freshness->setEnabled(usesFreshCache);

    if (usesFreshCache) {
        m_summary->setText(
            i18n("Recently visited folders can open faster. "
                 "Changes made through Dolphin still check the remote."));
    } else {
        m_summary->setText(
            i18n("Opening a folder always requests a new listing "
                 "from rclone and the remote provider."));
    }
}

void SettingsDialog::accept()
{
    const DirectoryListingPolicy policy = selectedPolicy();

    const bool policyChanged =
        policy.mode != m_initialPolicy.mode
        || policy.freshnessSeconds != m_initialPolicy.freshnessSeconds;

    if (policyChanged) {
        DirectoryListingPolicyStore::save(policy);
    }

    // A changed freshness contract must take effect for workers which are
    // already alive as well as for the next Dolphin session.
    if (policyChanged || m_clearSavedListings->isChecked()) {
        DirectorySnapshotCache::clearPersistent();
    }

    QDialog::accept();
}
