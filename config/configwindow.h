/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclonebackend.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

class ConfigWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigWindow(QWidget *parent = nullptr);

private:
    void refreshRemotes();
    void addGoogleDrive();
    void reconnectSelected();
    void configureGoogleOAuth();
    void removeSelected();
    void openSelected();
    void openAdvancedConfiguration();
    void updateActions();
    [[nodiscard]] QString selectedRemote() const;
    [[nodiscard]] bool runInteractiveRclone(const QStringList &arguments, const QString &title, const QString &description);
    [[nodiscard]] bool validateRemoteName(const QString &name) const;

    RcloneBackend m_backend;
    QHash<QString, RcloneRemoteInfo> m_remoteInfo;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_remoteList = nullptr;
    QPushButton *m_googleOAuthButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_reconnectButton = nullptr;
    QPushButton *m_removeButton = nullptr;
};
