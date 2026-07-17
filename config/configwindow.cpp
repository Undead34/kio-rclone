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
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
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
} // namespace

ConfigWindow::ConfigWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(i18n("Rclone Remotes"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral(KIO_RCLONE_CONFIG_APP_ID)));
    resize(620, 430);

    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel(i18n("<h2>Cloud storage in Dolphin</h2>"), this);
    layout->addWidget(title);

    auto *description = new QLabel(i18n("KIO Rclone uses your normal rclone configuration. OAuth "
                                        "and provider credentials are handled directly by "
                                        "rclone, without KDE Online Accounts."),
                                   this);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_remoteList = new QListWidget(this);
    m_remoteList->setAlternatingRowColors(true);
    m_remoteList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_remoteList, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto *primaryButtons = new QHBoxLayout;
    auto *addDriveButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Add Google Drive…"), this);
    auto *advancedButton = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), i18n("Other Providers…"), this);
    primaryButtons->addWidget(addDriveButton);
    primaryButtons->addWidget(advancedButton);
    primaryButtons->addStretch();
    layout->addLayout(primaryButtons);

    auto *remoteButtons = new QHBoxLayout;
    m_openButton = new QPushButton(QIcon::fromTheme(QStringLiteral("system-file-manager")), i18n("Open in Dolphin"), this);
    m_reconnectButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reconnect…"), this);
    m_googleOAuthButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit-sign-encrypt")), i18n("Google OAuth…"), this);
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Remove"), this);
    auto *refreshButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Refresh"), this);
    remoteButtons->addWidget(m_openButton);
    remoteButtons->addWidget(m_reconnectButton);
    remoteButtons->addWidget(m_googleOAuthButton);
    remoteButtons->addWidget(m_removeButton);
    remoteButtons->addStretch();
    remoteButtons->addWidget(refreshButton);
    layout->addLayout(remoteButtons);

    connect(addDriveButton, &QPushButton::clicked, this, [this]() {
        addGoogleDrive();
    });
    connect(advancedButton, &QPushButton::clicked, this, [this]() {
        openAdvancedConfiguration();
    });
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        openSelected();
    });
    connect(m_reconnectButton, &QPushButton::clicked, this, [this]() {
        reconnectSelected();
    });
    connect(m_googleOAuthButton, &QPushButton::clicked, this, [this]() {
        configureGoogleOAuth();
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
    const QStringList remotes = m_backend.remotes(&error);
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

void ConfigWindow::addGoogleDrive()
{
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, i18n("Add Google Drive"), i18n("Name shown in Dolphin:"), QLineEdit::Normal, QStringLiteral("Google Drive"), &accepted)
            .trimmed();
    if (!accepted || name.isEmpty()) {
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

    const bool success = runInteractiveRclone(
        {
            QStringLiteral("config"),
            QStringLiteral("create"),
            name,
            QStringLiteral("drive"),
            QStringLiteral("scope"),
            QStringLiteral("drive"),
            QStringLiteral("--no-output"),
        },
        i18n("Connect Google Drive"),
        i18n("Complete authorization in the browser window opened by rclone. "
             "This application never receives or displays your OAuth token."));

    refreshRemotes();
    if (success) {
        const auto matches = m_remoteList->findItems(name, Qt::MatchExactly);
        if (!matches.isEmpty()) {
            m_remoteList->setCurrentItem(matches.constFirst());
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

void ConfigWindow::configureGoogleOAuth()
{
    const QString remote = selectedRemote();
    if (remote.isEmpty() || m_remoteInfo.value(remote).type != QLatin1String("drive")) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Google OAuth for %1", remote));
    dialog.setMinimumWidth(560);

    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        i18n("Use your own Google OAuth desktop client to avoid the shared rclone quota. "
             "<a href=\"https://rclone.org/drive/#making-your-own-client-id\">Creation instructions</a>"),
        &dialog);
    description->setWordWrap(true);
    description->setOpenExternalLinks(true);
    layout->addWidget(description);

    auto *form = new QFormLayout;
    auto *clientId = new QLineEdit(&dialog);
    auto *clientSecret = new QLineEdit(&dialog);
    clientSecret->setEchoMode(QLineEdit::Password);
    form->addRow(i18n("Client ID:"), clientId);
    form->addRow(i18n("Client secret:"), clientSecret);
    layout->addLayout(form);

    if (const auto existing = existingGoogleOAuthOverride()) {
        auto *importButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-import")), i18n("Import existing local OAuth override"), &dialog);
        layout->addWidget(importButton);
        connect(importButton, &QPushButton::clicked, &dialog, [clientId, clientSecret, existing]() {
            clientId->setText(existing->first);
            clientSecret->setText(existing->second);
        });
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (clientId->text().trimmed().isEmpty() || clientSecret->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, i18n("Missing Credentials"), i18n("Enter both the Google OAuth client ID and client secret."));
        return;
    }

    const bool success = runInteractiveRclone(
        {
            QStringLiteral("config"),
            QStringLiteral("update"),
            remote,
            QStringLiteral("client_id"),
            clientId->text().trimmed(),
            QStringLiteral("client_secret"),
            clientSecret->text().trimmed(),
            QStringLiteral("--no-output"),
        },
        i18n("Authorize %1", remote),
        i18n("Complete authorization in the browser. rclone will store the new token directly in its own configuration."));
    refreshRemotes();
    if (!success) {
        QMessageBox::warning(this,
                             i18n("Authorization Incomplete"),
                             i18n("The Google OAuth credentials were not activated. Open Google OAuth again and complete the browser authorization."));
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
    const RcloneResult result = m_backend.run({QStringLiteral("config"), QStringLiteral("delete"), remote}, 30000);
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
    m_googleOAuthButton->setEnabled(selected && m_remoteInfo.value(remote).type == QLatin1String("drive"));
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
