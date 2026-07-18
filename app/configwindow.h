/*
 * SPDX-FileCopyrightText: 2026 KIO Rclone contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "rclonebackend.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

#include <optional>

class QLabel;
class QListWidget;
class QPushButton;

// One non-advanced option of an rclone backend, as declared by
// `rclone config providers`. The Add Remote form is generated from these so
// the fields always match what rclone actually accepts per provider.
struct ProviderFieldOption {
    QString name;
    QString help;
    QString defaultValue;
    QStringList examples;
    bool isPassword = false;
    bool required = false;
    bool exclusive = false;
};

class ConfigWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigWindow(QWidget *parent = nullptr);

private:
    void refreshRemotes();
    void addRemote();
    [[nodiscard]] QList<ProviderFieldOption> providerOptions(const QString &type);
    bool submitDriveLikeRemote(const QString &name, const QString &type, const QList<QPair<QString, QString>> &options = {});
    bool submitOneDriveRemote(const QString &name, const QList<QPair<QString, QString>> &options = {});
    [[nodiscard]] bool runOneDriveStep(const QStringList &arguments, const QString &statusText, QJsonObject *result);
    [[nodiscard]] std::optional<QString> promptOneDriveDriveId(const QJsonObject &option);
    void reconnectSelected();
    void editSelected();
    [[nodiscard]] bool renameRemote(const QString &oldName, const QString &newName);
    [[nodiscard]] RcloneResult runBackendWithPasswordRetry(const QStringList &arguments, int timeoutMs = 30000);
    [[nodiscard]] bool isConfigPasswordError(const QString &diagnostic) const;
    [[nodiscard]] bool promptConfigPassword();
    void removeSelected();
    void openSelected();
    void openAdvancedConfiguration();
    void updateActions();
    [[nodiscard]] QString selectedRemote() const;
    [[nodiscard]] bool runInteractiveRclone(const QStringList &arguments, const QString &title, const QString &description);
    [[nodiscard]] bool validateRemoteName(const QString &name) const;

    RcloneBackend m_backend;
    QHash<QString, RcloneRemoteInfo> m_remoteInfo;
    std::optional<QJsonArray> m_providersCache;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_remoteList = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QPushButton *m_reconnectButton = nullptr;
    QPushButton *m_removeButton = nullptr;
};
