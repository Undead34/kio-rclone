/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "appid.h"
#include "configwindow.h"
#include "rcloneurl.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace
{
std::optional<QPair<QString, QString>> existingGoogleOAuthOverride()
{
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/accounts/providers/google.provider"));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    QString clientId;
    QString clientSecret;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("setting")) {
            continue;
        }

        const QString name = xml.attributes().value(QStringLiteral("name")).toString();
        if (name == QLatin1String("ClientId")) {
            clientId = xml.readElementText().trimmed();
        } else if (name == QLatin1String("ClientSecret")) {
            clientSecret = xml.readElementText().trimmed();
        }
    }

    if (xml.hasError() || clientId.isEmpty() || clientSecret.isEmpty()) {
        return std::nullopt;
    }
    return qMakePair(clientId, clientSecret);
}

// Per-provider lookup for OAuth credentials already configured elsewhere on
// this machine. Only "drive" has a known local source today (KDE System
// Settings' Google account integration); adding a source for another
// curated provider only means adding a case here — the "Add Remote" dialog
// already reacts to whatever this returns for the selected provider.
std::optional<QPair<QString, QString>> existingOAuthOverride(const QString &type)
{
    if (type == QLatin1String("drive")) {
        return existingGoogleOAuthOverride();
    }
    return std::nullopt;
}

struct CuratedProvider {
    QString type;
    QString label;
    QString description;
};

// Sentinel entry: not a real rclone backend type, selecting it in "Add Remote"
// hands off to the terminal-based advanced flow instead of a create command.
const QString OtherProvidersType = QStringLiteral("__other__");

const QList<CuratedProvider> &curatedProviders()
{
    static const QList<CuratedProvider> providers = {
        {QStringLiteral("drive"),
         i18n("Google Drive"),
         i18n("rclone opens a browser window to authorize with Google. This application never receives or displays your OAuth token.")},
        {QStringLiteral("onedrive"),
         i18n("OneDrive"),
         i18n("rclone opens a browser window to authorize with Microsoft, then you will be asked which drive to use.")},
        {QStringLiteral("dropbox"),
         i18n("Dropbox"),
         i18n("rclone opens a browser window to authorize with Dropbox. This application never receives or displays your OAuth token.")},
        {OtherProvidersType,
         i18n("Other Providers…"),
         i18n("Opens a terminal running rclone's own configuration wizard, for any backend not listed above (S3, WebDAV, SFTP, and more).")},
    };
    return providers;
}

QString curatedProviderLabel(const QString &type)
{
    for (const auto &provider : curatedProviders()) {
        if (provider.type == type) {
            return provider.label;
        }
    }
    return type;
}

struct OAuthProviderInfo {
    QString linkText;
    QString linkUrl;
};

const QHash<QString, OAuthProviderInfo> &customOAuthProviders()
{
    static const QHash<QString, OAuthProviderInfo> providers = {
        {QStringLiteral("drive"), {i18n("Creation instructions"), QStringLiteral("https://rclone.org/drive/#making-your-own-client-id")}},
        {QStringLiteral("onedrive"), {i18n("Creation instructions"), QStringLiteral("https://rclone.org/onedrive/#getting-your-own-client-id-and-key")}},
        {QStringLiteral("dropbox"), {i18n("Creation instructions"), QStringLiteral("https://rclone.org/dropbox/#get-your-own-dropbox-app-id")}},
    };
    return providers;
}
} // namespace

ConfigWindow::ConfigWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(i18n("Rclone Remotes"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID)));
    resize(620, 430);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(i18n("<h2>Cloud storage in Dolphin</h2>"), this);
    layout->addWidget(title);

    auto *description = new QLabel(i18n("KIO Rclone uses your normal rclone configuration. OAuth "
                                        "and provider credentials are handled directly by "
                                        "rclone, without KDE Online Accounts."),
                                   this);
    description->setWordWrap(true);
    layout->addWidget(description);

    layout->addSpacing(4);

    m_remoteList = new QListWidget(this);
    m_remoteList->setAlternatingRowColors(true);
    m_remoteList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_remoteList, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // Global actions: available regardless of whether a remote is selected.
    auto *globalButtons = new QHBoxLayout;
    globalButtons->setSpacing(8);
    auto *addRemoteButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add Remote…"), this);
    auto *refreshButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Refresh"), this);
    globalButtons->addWidget(addRemoteButton);
    globalButtons->addWidget(refreshButton);
    globalButtons->addStretch();
    layout->addLayout(globalButtons);

    // Actions on the selected remote, ordered by how often they're used:
    // open it, edit it, reconnect it, and — separated as the destructive one
    // — remove it. Enablement follows the same grouping in updateActions().
    auto *remoteButtons = new QHBoxLayout;
    remoteButtons->setSpacing(8);
    m_openButton = new QPushButton(QIcon::fromTheme(QStringLiteral("system-file-manager")), i18n("Open in Dolphin"), this);
    m_editButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18n("Edit…"), this);
    m_reconnectButton = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")), i18n("Reconnect…"), this);
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Remove"), this);
    remoteButtons->addWidget(m_openButton);
    remoteButtons->addWidget(m_editButton);
    remoteButtons->addWidget(m_reconnectButton);
    remoteButtons->addStretch();
    remoteButtons->addWidget(m_removeButton);
    layout->addLayout(remoteButtons);

    connect(addRemoteButton, &QPushButton::clicked, this, [this]() {
        addRemote();
    });
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        openSelected();
    });
    connect(m_reconnectButton, &QPushButton::clicked, this, [this]() {
        reconnectSelected();
    });
    connect(m_editButton, &QPushButton::clicked, this, [this]() {
        editSelected();
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this]() {
        removeSelected();
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        refreshRemotes();
    });
    connect(m_remoteList, &QListWidget::itemSelectionChanged, this, [this]() {
        updateActions();
    });
    connect(m_remoteList, &QListWidget::itemDoubleClicked, this, [this]() {
        openSelected();
    });

    refreshRemotes();
}

bool ConfigWindow::isConfigPasswordError(const QString &diagnostic) const
{
    return diagnostic.contains(QStringLiteral("Enter configuration password")) || diagnostic.contains(QStringLiteral("Couldn't decrypt configuration"))
        || diagnostic.contains(QStringLiteral("unable to decrypt configuration"));
}

bool ConfigWindow::promptConfigPassword()
{
    bool accepted = false;
    const QString password = QInputDialog::getText(this,
                                                    i18n("Unlock Configuration"),
                                                    i18n("Your rclone configuration is encrypted. Enter its existing password "
                                                         "to unlock it for this session — this does not change the password."),
                                                    QLineEdit::Password,
                                                    QString(),
                                                    &accepted);
    if (!accepted || password.isEmpty()) {
        return false;
    }
    // Set for the remainder of this process: every subsequent RcloneBackend::run()
    // call (in this class and QProcess calls below) reads the real environment,
    // so this single prompt covers the whole session, not just the current call.
    qputenv("RCLONE_CONFIG_PASS", password.toUtf8());
    return true;
}

RcloneResult ConfigWindow::runBackendWithPasswordRetry(const QStringList &arguments, int timeoutMs)
{
    RcloneResult result = m_backend.run(arguments, timeoutMs);
    if (!result.success() && isConfigPasswordError(result.errorMessage()) && promptConfigPassword()) {
        return m_backend.run(arguments, timeoutMs);
    }
    return result;
}

void ConfigWindow::refreshRemotes()
{
    m_remoteList->clear();
    m_remoteInfo.clear();
    if (!m_backend.isAvailable()) {
        m_statusLabel->setText(
            i18n("rclone was not found. Install it or set "
                 "KIO_RCLONE_EXECUTABLE to its full path."));
        updateActions();
        return;
    }

    QString error;
    QStringList remotes = m_backend.remotes(&error);
    if (remotes.isEmpty() && isConfigPasswordError(error) && promptConfigPassword()) {
        error.clear();
        remotes = m_backend.remotes(&error);
    }
    int sharedGoogleClients = 0;
    for (const QString &remote : remotes) {
        QString infoError;
        const auto info = m_backend.remoteInfo(remote, &infoError);
        if (info) {
            m_remoteInfo.insert(remote, *info);
        }

        const bool sharedGoogleClient = info && info->type == QLatin1String("drive") && !info->hasClientId;
        auto *item =
            new QListWidgetItem(QIcon::fromTheme(sharedGoogleClient ? QStringLiteral("dialog-warning") : QStringLiteral("folder-cloud")), remote, m_remoteList);
        item->setData(Qt::UserRole, remote);
        if (sharedGoogleClient) {
            ++sharedGoogleClients;
            item->setToolTip(i18n("This Google Drive remote uses rclone's shared OAuth client. "
                                  "It is quota-limited and is being retired during 2026."));
        } else if (!infoError.isEmpty()) {
            item->setToolTip(infoError);
        }
    }

    if (!error.isEmpty()) {
        m_statusLabel->setText(error);
    } else if (remotes.isEmpty()) {
        m_statusLabel->setText(i18n("No remotes are configured yet."));
    } else if (sharedGoogleClients > 0) {
        m_statusLabel->setText(
            i18np("One Google Drive remote uses rclone's shared OAuth client. Configure your own client to avoid quota delays and service interruption.",
                  "%1 Google Drive remotes use rclone's shared OAuth client. Configure your own clients to avoid quota delays and service interruption.",
                  sharedGoogleClients));
    } else {
        m_statusLabel->setText(i18np("One remote configured.", "%1 remotes configured.", remotes.size()));
        m_remoteList->setCurrentRow(0);
    }
    updateActions();
}

void ConfigWindow::addRemote()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Add Remote"));
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    // Green tip banner: shown when an OAuth client for the selected provider
    // is already configured locally. "Use" loads the values into the visible
    // form fields so they can be reviewed or edited before creating.
    auto *banner = new QFrame(&dialog);
    banner->setObjectName(QStringLiteral("tipBanner"));
    banner->setStyleSheet(QStringLiteral("QFrame#tipBanner { background-color: rgba(39, 174, 96, 38); "
                                         "border: 1px solid rgba(39, 174, 96, 110); border-radius: 4px; }"
                                         "QFrame#tipBanner QLabel { background: transparent; border: none; }"));
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(10, 6, 10, 6);
    auto *bannerLabel = new QLabel(banner);
    bannerLabel->setWordWrap(true);
    auto *bannerButton = new QPushButton(i18n("Use"), banner);
    bannerLayout->addWidget(bannerLabel, 1);
    bannerLayout->addWidget(bannerButton);
    banner->setVisible(false);
    layout->addWidget(banner);

    auto *form = new QFormLayout;
    form->setSpacing(8);
    auto *providerCombo = new QComboBox(&dialog);
    for (const auto &provider : curatedProviders()) {
        providerCombo->addItem(provider.label);
    }
    auto *nameEdit = new QLineEdit(&dialog);
    form->addRow(i18n("Provider:"), providerCombo);
    form->addRow(i18n("Name:"), nameEdit);

    auto *authRow = new QHBoxLayout;
    auto *defaultAuthRadio = new QRadioButton(i18n("rclone defaults"), &dialog);
    auto *customAuthRadio = new QRadioButton(i18n("Custom"), &dialog);
    defaultAuthRadio->setChecked(true);
    defaultAuthRadio->setToolTip(i18n("Authorize with rclone's shared OAuth client."));
    customAuthRadio->setToolTip(i18n("Provide your own connection values before authorizing."));
    authRow->addWidget(defaultAuthRadio);
    authRow->addWidget(customAuthRadio);
    authRow->addStretch();
    form->addRow(i18n("Configuration:"), authRow);
    layout->addLayout(form);

    // Provider-specific fields, generated from `rclone config providers` so
    // they always match what rclone actually accepts for the selected backend.
    auto *fieldsWidget = new QWidget(&dialog);
    auto *fieldsForm = new QFormLayout(fieldsWidget);
    fieldsForm->setContentsMargins(0, 0, 0, 0);
    fieldsForm->setSpacing(8);
    fieldsWidget->setVisible(false);
    layout->addWidget(fieldsWidget);

    auto *otherHint = new QLabel(i18n("Opens rclone's terminal wizard for every other backend."), &dialog);
    otherHint->setWordWrap(true);
    otherHint->setVisible(false);
    layout->addWidget(otherHint);

    QList<QPair<ProviderFieldOption, QWidget *>> fieldWidgets;
    std::optional<QPair<QString, QString>> detectedOverride;

    const auto fieldValue = [](QWidget *editor) -> QString {
        if (const auto *combo = qobject_cast<const QComboBox *>(editor)) {
            return combo->currentText().trimmed();
        }
        return static_cast<const QLineEdit *>(editor)->text().trimmed();
    };
    const auto setFieldValue = [&fieldWidgets](const QString &optionName, const QString &value) {
        for (const auto &field : fieldWidgets) {
            if (field.first.name != optionName) {
                continue;
            }
            if (auto *combo = qobject_cast<QComboBox *>(field.second)) {
                combo->setCurrentText(value);
            } else {
                static_cast<QLineEdit *>(field.second)->setText(value);
            }
        }
    };

    const auto rebuildFields = [this, fieldsForm, &fieldWidgets, &dialog](const QString &type) {
        fieldWidgets.clear();
        while (QLayoutItem *item = fieldsForm->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const QList<ProviderFieldOption> options = providerOptions(type);
        for (const ProviderFieldOption &option : options) {
            QWidget *editor = nullptr;
            if (!option.examples.isEmpty()) {
                auto *combo = new QComboBox(&dialog);
                combo->setEditable(!option.exclusive);
                combo->addItems(option.examples);
                combo->setCurrentText(option.defaultValue);
                editor = combo;
            } else {
                auto *edit = new QLineEdit(option.defaultValue, &dialog);
                if (option.isPassword || option.name.contains(QLatin1String("secret"))) {
                    edit->setEchoMode(QLineEdit::Password);
                }
                editor = edit;
            }
            editor->setToolTip(option.help);
            const QString label = option.required ? i18n("%1 *", option.name) : option.name;
            fieldsForm->addRow(label, editor);
            fieldWidgets.append({option, editor});
        }
    };

    // Hiding the custom fields does not shrink the dialog on its own, and any
    // leftover vertical space would be absorbed by the banner (the only widget
    // able to grow). Resize back to the natural size once the pending
    // visibility change has been processed by the layout.
    const auto shrinkToFit = [&dialog]() {
        QTimer::singleShot(0, &dialog, [&dialog]() {
            dialog.resize(dialog.sizeHint());
        });
    };

    QString previousLabel;
    const auto onProviderChanged = [&, this](int index) {
        const CuratedProvider &provider = curatedProviders().at(index);
        const bool isOther = provider.type == OtherProvidersType;

        nameEdit->setEnabled(!isOther);
        defaultAuthRadio->setVisible(!isOther);
        customAuthRadio->setVisible(!isOther);
        otherHint->setVisible(isOther);
        if (isOther) {
            nameEdit->clear();
        } else if (nameEdit->text().trimmed() == previousLabel || nameEdit->text().trimmed().isEmpty()) {
            nameEdit->setText(provider.label);
        }
        previousLabel = provider.label;

        if (!isOther) {
            rebuildFields(provider.type);
        }
        fieldsWidget->setVisible(!isOther && customAuthRadio->isChecked());

        detectedOverride = existingOAuthOverride(provider.type);
        banner->setVisible(!isOther && detectedOverride.has_value());
        if (detectedOverride) {
            bannerLabel->setText(i18n("An OAuth client for %1 was found in KDE System Settings.", provider.label));
        }
        shrinkToFit();
    };
    onProviderChanged(0);
    connect(providerCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, onProviderChanged);
    connect(customAuthRadio, &QRadioButton::toggled, &dialog, [&](bool checked) {
        const bool isOther = curatedProviders().at(providerCombo->currentIndex()).type == OtherProvidersType;
        fieldsWidget->setVisible(checked && !isOther);
        shrinkToFit();
    });
    connect(bannerButton, &QPushButton::clicked, &dialog, [&]() {
        if (!detectedOverride) {
            return;
        }
        customAuthRadio->setChecked(true);
        setFieldValue(QStringLiteral("client_id"), detectedOverride->first);
        setFieldValue(QStringLiteral("client_secret"), detectedOverride->second);
    });

    // Any surplus vertical space (e.g. between hiding the custom fields and
    // the deferred shrink) collapses here instead of stretching the banner.
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (curatedProviders().at(providerCombo->currentIndex()).type == OtherProvidersType) {
        openAdvancedConfiguration();
        return;
    }

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        return;
    }
    if (!validateRemoteName(name)) {
        QMessageBox::warning(this, i18n("Invalid Name"), i18n("Remote names cannot contain ':', '/', or '\\'."));
        return;
    }

    QString error;
    if (m_backend.remotes(&error).contains(name)) {
        QMessageBox::warning(this, i18n("Remote Exists"), i18n("A remote named “%1” already exists.", name));
        return;
    }
    if (!error.isEmpty()) {
        QMessageBox::critical(this, i18n("Rclone Error"), error);
        return;
    }

    QList<QPair<QString, QString>> options;
    if (customAuthRadio->isChecked()) {
        for (const auto &field : fieldWidgets) {
            const QString value = fieldValue(field.second);
            if (field.first.required && value.isEmpty()) {
                QMessageBox::warning(this, i18n("Missing Value"), i18n("“%1” is required for this provider.", field.first.name));
                return;
            }
            if (!value.isEmpty() && value != field.first.defaultValue) {
                options.append({field.first.name, value});
            }
        }
    }

    const QString type = curatedProviders().at(providerCombo->currentIndex()).type;
    const bool success = type == QLatin1String("onedrive") ? submitOneDriveRemote(name, options) : submitDriveLikeRemote(name, type, options);

    refreshRemotes();
    if (success) {
        const auto matches = m_remoteList->findItems(name, Qt::MatchExactly);
        if (!matches.isEmpty()) {
            m_remoteList->setCurrentItem(matches.constFirst());
        }
    }
}

QList<ProviderFieldOption> ConfigWindow::providerOptions(const QString &type)
{
    if (!m_providersCache) {
        const RcloneResult result = runBackendWithPasswordRetry({QStringLiteral("config"), QStringLiteral("providers")});
        if (!result.success()) {
            return {};
        }
        const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput);
        if (!document.isArray()) {
            return {};
        }
        m_providersCache = document.array();
    }

    for (const QJsonValue &providerValue : *m_providersCache) {
        const QJsonObject provider = providerValue.toObject();
        if (provider.value(QStringLiteral("Name")).toString() != type) {
            continue;
        }

        QList<ProviderFieldOption> options;
        const QJsonArray rawOptions = provider.value(QStringLiteral("Options")).toArray();
        for (const QJsonValue &optionValue : rawOptions) {
            const QJsonObject option = optionValue.toObject();
            if (option.value(QStringLiteral("Advanced")).toBool() || option.value(QStringLiteral("Hide")).toInt() != 0) {
                continue;
            }
            ProviderFieldOption field;
            field.name = option.value(QStringLiteral("Name")).toString();
            field.help = option.value(QStringLiteral("Help")).toString();
            field.defaultValue = option.value(QStringLiteral("DefaultStr")).toString();
            field.isPassword = option.value(QStringLiteral("IsPassword")).toBool();
            field.required = option.value(QStringLiteral("Required")).toBool();
            field.exclusive = option.value(QStringLiteral("Exclusive")).toBool();
            const QJsonArray examples = option.value(QStringLiteral("Examples")).toArray();
            for (const QJsonValue &exampleValue : examples) {
                field.examples << exampleValue.toObject().value(QStringLiteral("Value")).toString();
            }
            options.append(field);
        }
        return options;
    }
    return {};
}

bool ConfigWindow::submitDriveLikeRemote(const QString &name, const QString &type, const QList<QPair<QString, QString>> &options)
{
    QStringList arguments = {
        QStringLiteral("config"),
        QStringLiteral("create"),
        name,
        type,
    };
    if (type == QLatin1String("drive")) {
        // Full-access scope keeps parity with what the worker needs; a custom
        // scope value from the form overrides this pair below (rclone keeps
        // the last occurrence of a repeated key).
        arguments << QStringLiteral("scope") << QStringLiteral("drive");
    }
    for (const auto &option : options) {
        arguments << option.first << option.second;
    }
    arguments << QStringLiteral("--no-output");

    return runInteractiveRclone(arguments,
                                i18n("Connect %1", name),
                                i18n("Complete authorization in the browser window opened by rclone. "
                                     "This application never receives or displays your OAuth token."));
}

bool ConfigWindow::runOneDriveStep(const QStringList &arguments, const QString &statusText, QJsonObject *result)
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Connect OneDrive"));
    dialog.setWindowIcon(QIcon::fromTheme(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID)));
    dialog.setMinimumWidth(500);

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(statusText, &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *progress = new QProgressBar(&dialog);
    progress->setRange(0, 0);
    layout->addWidget(progress);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    QProcess process(&dialog);
    process.setProgram(m_backend.executable());
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);

    QByteArray standardOutput;
    QString standardError;
    bool succeeded = false;
    connect(&process, &QProcess::readyReadStandardOutput, &dialog, [&]() {
        standardOutput += process.readAllStandardOutput();
    });
    connect(&process, &QProcess::readyReadStandardError, &dialog, [&]() {
        standardError += QString::fromUtf8(process.readAllStandardError());
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &dialog, [&](int exitCode, QProcess::ExitStatus exitStatus) {
        succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, [&]() {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(1000)) {
                process.kill();
            }
        }
        dialog.reject();
    });

    process.start();
    if (!process.waitForStarted(5000)) {
        QMessageBox::critical(this, i18n("Rclone Error"), process.errorString());
        return false;
    }

    if (dialog.exec() != QDialog::Accepted || !succeeded) {
        if (!standardError.trimmed().isEmpty()) {
            QMessageBox::critical(this, i18n("Rclone Error"), standardError.trimmed());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(standardOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::critical(this, i18n("Rclone Error"), i18n("Could not parse rclone's response."));
        return false;
    }

    *result = document.object();
    return true;
}

std::optional<QString> ConfigWindow::promptOneDriveDriveId(const QJsonObject &option)
{
    const QJsonArray examples = option.value(QStringLiteral("Examples")).toArray();
    QStringList labels;
    QStringList values;
    for (const auto &exampleValue : examples) {
        const QJsonObject example = exampleValue.toObject();
        values << example.value(QStringLiteral("Value")).toString();
        labels << example.value(QStringLiteral("Help")).toString();
    }

    bool accepted = false;
    const QString choice =
        QInputDialog::getItem(this, i18n("Select OneDrive Drive"), i18n("Choose which drive this remote should use:"), labels, 0, false, &accepted);
    if (!accepted) {
        return std::nullopt;
    }

    const int index = labels.indexOf(choice);
    return index >= 0 ? std::optional<QString>(values.at(index)) : std::nullopt;
}

bool ConfigWindow::submitOneDriveRemote(const QString &name, const QList<QPair<QString, QString>> &options)
{
    const QString browserStatus = i18n("Complete authorization in the browser window opened by rclone, if one appears. "
                                       "This application never receives or displays your OAuth token.");

    QStringList createArguments = {QStringLiteral("config"), QStringLiteral("create"), name, QStringLiteral("onedrive")};
    for (const auto &option : options) {
        createArguments << option.first << option.second;
    }
    createArguments << QStringLiteral("--non-interactive");

    QString state;
    QString result;
    bool first = true;
    while (true) {
        const QStringList arguments = first
            ? createArguments
            : QStringList{QStringLiteral("config"),
                          QStringLiteral("update"),
                          name,
                          QStringLiteral("--non-interactive"),
                          QStringLiteral("--continue"),
                          QStringLiteral("--state"),
                          state,
                          QStringLiteral("--result"),
                          result};
        first = false;

        QJsonObject response;
        if (!runOneDriveStep(arguments, browserStatus, &response)) {
            return false;
        }

        const QString stepError = response.value(QStringLiteral("Error")).toString();
        if (!stepError.isEmpty()) {
            QMessageBox::critical(this, i18n("Rclone Error"), stepError);
            return false;
        }

        state = response.value(QStringLiteral("State")).toString();
        if (state.isEmpty()) {
            return true;
        }

        const QJsonObject option = response.value(QStringLiteral("Option")).toObject();
        if (option.value(QStringLiteral("Name")).toString() == QLatin1String("config_driveid")) {
            const auto chosen = promptOneDriveDriveId(option);
            if (!chosen) {
                return false;
            }
            result = *chosen;
        } else {
            result = option.value(QStringLiteral("DefaultStr")).toString();
        }
    }
}

void ConfigWindow::reconnectSelected()
{
    const QString remote = selectedRemote();
    if (remote.isEmpty()) {
        return;
    }

    if (!runInteractiveRclone({QStringLiteral("config"), QStringLiteral("reconnect"), remote + QLatin1Char(':')},
                              i18n("Reconnect %1", remote),
                              i18n("Complete authorization in the browser window opened by rclone."))) {
        refreshRemotes();
        return;
    }
    refreshRemotes();
}

bool ConfigWindow::renameRemote(const QString &oldName, const QString &newName)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const RcloneResult dumpResult = runBackendWithPasswordRetry({QStringLiteral("config"), QStringLiteral("dump")});
    if (!dumpResult.success()) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, i18n("Rclone Error"), dumpResult.errorMessage());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(dumpResult.standardOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !document.object().contains(oldName)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, i18n("Rclone Error"), i18n("Could not read the current remote configuration."));
        return false;
    }

    // A pure copy under the new name with the exact same keys (including the
    // already-obscured token) needs no re-authorization: rclone never touches
    // the browser for this, unlike a client_id/secret change further below.
    const QJsonObject remoteConfig = document.object().value(oldName).toObject();
    QStringList arguments = {QStringLiteral("config"), QStringLiteral("create"), newName, remoteConfig.value(QStringLiteral("type")).toString()};
    for (auto it = remoteConfig.constBegin(); it != remoteConfig.constEnd(); ++it) {
        if (it.key() == QLatin1String("type")) {
            continue;
        }
        arguments << it.key() << it.value().toString();
    }
    arguments << QStringLiteral("--no-obscure") << QStringLiteral("--no-output");

    const RcloneResult createResult = runBackendWithPasswordRetry(arguments);
    if (!createResult.success()) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, i18n("Rclone Error"), createResult.errorMessage());
        return false;
    }

    const RcloneResult deleteResult = runBackendWithPasswordRetry({QStringLiteral("config"), QStringLiteral("delete"), oldName});
    QApplication::restoreOverrideCursor();
    if (!deleteResult.success()) {
        QMessageBox::warning(
            this,
            i18n("Rename Incomplete"),
            i18n("“%1” was created as a copy of “%2”, but the old entry could not be removed automatically: %3\nRemove it manually to finish the rename.",
                 newName,
                 oldName,
                 deleteResult.errorMessage()));
    }
    return true;
}

void ConfigWindow::editSelected()
{
    const QString remote = selectedRemote();
    if (remote.isEmpty()) {
        return;
    }
    const QString type = m_remoteInfo.value(remote).type;
    const bool supportsCustomOAuth = customOAuthProviders().contains(type);

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Edit %1", remote));
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *nameForm = new QFormLayout;
    auto *nameEdit = new QLineEdit(remote, &dialog);
    nameForm->addRow(i18n("Name shown in Dolphin:"), nameEdit);
    layout->addLayout(nameForm);

    QLineEdit *clientId = nullptr;
    QLineEdit *clientSecret = nullptr;
    QCheckBox *suggestionCheckbox = nullptr;
    std::optional<QPair<QString, QString>> detectedOverride;

    if (supportsCustomOAuth) {
        const OAuthProviderInfo &info = customOAuthProviders().value(type);

        auto *separator = new QFrame(&dialog);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        layout->addWidget(separator);

        auto *credentialsDescription = new QLabel(
            i18n("Use your own OAuth desktop client to avoid rclone's shared quota. "
                 "<a href=\"%1\">%2</a>",
                 info.linkUrl,
                 info.linkText),
            &dialog);
        credentialsDescription->setWordWrap(true);
        credentialsDescription->setOpenExternalLinks(true);
        layout->addWidget(credentialsDescription);

        auto *credentialsForm = new QFormLayout;
        clientId = new QLineEdit(&dialog);
        clientSecret = new QLineEdit(&dialog);
        clientSecret->setEchoMode(QLineEdit::Password);
        credentialsForm->addRow(i18n("Client ID:"), clientId);
        credentialsForm->addRow(i18n("Client secret:"), clientSecret);
        layout->addLayout(credentialsForm);

        detectedOverride = existingOAuthOverride(type);
        if (detectedOverride) {
            auto *suggestionLabel = new QLabel(
                i18n("A custom OAuth client for %1 is already configured in KDE System Settings.", curatedProviderLabel(type)), &dialog);
            suggestionLabel->setWordWrap(true);
            layout->addWidget(suggestionLabel);

            suggestionCheckbox = new QCheckBox(i18n("Use it for this remote"), &dialog);
            layout->addWidget(suggestionCheckbox);
            connect(suggestionCheckbox, &QCheckBox::toggled, &dialog, [clientId, clientSecret, detectedOverride](bool checked) {
                clientId->setText(checked ? detectedOverride->first : QString());
                clientSecret->setText(checked ? detectedOverride->second : QString());
                clientId->setReadOnly(checked);
                clientSecret->setReadOnly(checked);
            });
            suggestionCheckbox->setChecked(true);
        }
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) {
        return;
    }

    QString effectiveName = remote;
    if (newName != remote) {
        if (!validateRemoteName(newName)) {
            QMessageBox::warning(this, i18n("Invalid Name"), i18n("Remote names cannot contain ':', '/', or '\\'."));
            return;
        }
        QString error;
        if (m_backend.remotes(&error).contains(newName)) {
            QMessageBox::warning(this, i18n("Remote Exists"), i18n("A remote named “%1” already exists.", newName));
            return;
        }
        if (!renameRemote(remote, newName)) {
            return;
        }
        effectiveName = newName;
    }

    const QString newClientId = clientId ? clientId->text().trimmed() : QString();
    const QString newClientSecret = clientSecret ? clientSecret->text().trimmed() : QString();
    if (!newClientId.isEmpty() && !newClientSecret.isEmpty()) {
        const bool success = runInteractiveRclone(
            {
                QStringLiteral("config"),
                QStringLiteral("update"),
                effectiveName,
                QStringLiteral("client_id"),
                newClientId,
                QStringLiteral("client_secret"),
                newClientSecret,
                QStringLiteral("--no-output"),
            },
            i18n("Authorize %1", effectiveName),
            i18n("Complete authorization in the browser. rclone will store the new token directly in its own configuration."));
        if (!success) {
            QMessageBox::warning(this,
                                 i18n("Authorization Incomplete"),
                                 i18n("The OAuth credentials were not activated. Open Edit again and complete the browser authorization."));
        }
    }

    refreshRemotes();
    const auto matches = m_remoteList->findItems(effectiveName, Qt::MatchExactly);
    if (!matches.isEmpty()) {
        m_remoteList->setCurrentItem(matches.constFirst());
    }
}

void ConfigWindow::removeSelected()
{
    const QString remote = selectedRemote();
    if (remote.isEmpty()) {
        return;
    }

    const auto answer = QMessageBox::question(this,
                                              i18n("Remove Remote"),
                                              i18n("Remove “%1” from the rclone configuration?\n\nNo cloud files will "
                                                   "be deleted.",
                                                   remote));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const RcloneResult result = runBackendWithPasswordRetry({QStringLiteral("config"), QStringLiteral("delete"), remote});
    QApplication::restoreOverrideCursor();
    if (!result.success()) {
        QMessageBox::critical(this, i18n("Rclone Error"), result.errorMessage());
    }
    refreshRemotes();
}

void ConfigWindow::openSelected()
{
    const QString remote = selectedRemote();
    if (!remote.isEmpty()) {
        QDesktopServices::openUrl(RcloneUrl::remoteUrl(remote));
    }
}

void ConfigWindow::openAdvancedConfiguration()
{
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (!konsole.isEmpty()) {
        QProcess::startDetached(konsole, {QStringLiteral("-e"), m_backend.executable(), QStringLiteral("config")});
        return;
    }

    const QString xterm = QStandardPaths::findExecutable(QStringLiteral("xterm"));
    if (!xterm.isEmpty()) {
        QProcess::startDetached(xterm, {QStringLiteral("-e"), m_backend.executable(), QStringLiteral("config")});
        return;
    }

    QMessageBox::information(this,
                             i18n("Advanced Configuration"),
                             i18n("No supported terminal was found. Run this "
                                  "command manually:\n\n%1 config",
                                  m_backend.executable()));
}

void ConfigWindow::updateActions()
{
    const QString remote = selectedRemote();
    const bool selected = !remote.isEmpty();
    m_openButton->setEnabled(selected);
    m_reconnectButton->setEnabled(selected);
    m_editButton->setEnabled(selected);
    m_removeButton->setEnabled(selected);
}

QString ConfigWindow::selectedRemote() const
{
    const QListWidgetItem *item = m_remoteList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool ConfigWindow::runInteractiveRclone(const QStringList &arguments, const QString &title, const QString &description)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setWindowIcon(QIcon::fromTheme(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID)));
    dialog.setMinimumWidth(500);

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(description, &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *progress = new QProgressBar(&dialog);
    progress->setRange(0, 0);
    layout->addWidget(progress);

    auto *status = new QLabel(i18n("Waiting for rclone…"), &dialog);
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    QProcess process(&dialog);
    process.setProgram(m_backend.executable());
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);

    bool succeeded = false;
    QString diagnostic;
    connect(&process, &QProcess::readyRead, &dialog, [&]() {
        const QString output = QString::fromUtf8(process.readAll()).trimmed();
        if (output.contains(QStringLiteral("browser"), Qt::CaseInsensitive) || output.contains(QStringLiteral("authorize"), Qt::CaseInsensitive)) {
            status->setText(i18n("Authorization is waiting in your browser."));
        }
        if (output.contains(QStringLiteral("failed"), Qt::CaseInsensitive) || output.contains(QStringLiteral("error"), Qt::CaseInsensitive)) {
            diagnostic = output.left(2000);
        }
    });
    connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &dialog, [&](int exitCode, QProcess::ExitStatus exitStatus) {
        succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
        succeeded ? dialog.accept() : dialog.reject();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, [&]() {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(1000)) {
                process.kill();
            }
        }
        dialog.reject();
    });

    process.start();
    if (!process.waitForStarted(5000)) {
        QMessageBox::critical(this, i18n("Rclone Error"), process.errorString());
        return false;
    }

    dialog.exec();
    if (!succeeded && isConfigPasswordError(diagnostic)) {
        return promptConfigPassword() && runInteractiveRclone(arguments, title, description);
    }
    if (!succeeded && !diagnostic.isEmpty()) {
        QMessageBox::critical(this, i18n("Rclone Error"), diagnostic);
    }
    return succeeded;
}

bool ConfigWindow::validateRemoteName(const QString &name) const
{
    return !name.contains(QLatin1Char(':')) && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && !name.contains(QLatin1Char('\n'))
        && name != RcloneUrl::ConfigureEntry;
}
