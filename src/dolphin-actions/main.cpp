/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "driveactiontarget.h"
#include "drivemetadataresolver.h"
#include "googleurls.h"

#include <KLocalizedString>

#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDesktopServices>
#include <QMessageBox>

#include <optional>

namespace {

using DolphinDriveActions::DriveActionTarget;
using DolphinDriveActions::DriveMetadataResolver;
using DolphinDriveActions::WorkspaceFileType;

enum class Action {
    Open,
    CopyLink,
    OpenFolder,
    New,
};

struct Command {
    Action action;
    QUrl url;
    std::optional<WorkspaceFileType> workspaceType;
};

class CommandRcloneContext final : public RcloneContext
{
public:
    [[nodiscard]] bool isCancelled() const override
    {
        return false;
    }
};

int showError(const QString &message)
{
    QMessageBox::critical(
        nullptr,
        i18n("Google Drive action failed"),
        message);
    return 1;
}

void showUsage()
{
    QMessageBox::information(
        nullptr,
        i18n("Google Drive actions"),
        i18n("This command is launched by a Dolphin Google Drive action."));
}

std::optional<Command> parseCommand(const QStringList &arguments,
                                    QString *errorMessage)
{
    if (arguments.size() < 3) {
        *errorMessage = i18n("Missing Google Drive action arguments.");
        return std::nullopt;
    }

    Command command;
    const QString verb = arguments.at(1);
    qsizetype urlIndex = 2;

    if (verb == QLatin1String("open")) {
        command.action = Action::Open;
    } else if (verb == QLatin1String("copy-link")) {
        command.action = Action::CopyLink;
    } else if (verb == QLatin1String("open-folder")) {
        command.action = Action::OpenFolder;
    } else if (verb == QLatin1String("new")) {
        if (arguments.size() < 4) {
            *errorMessage = i18n("Missing the type of Google Workspace file to create.");
            return std::nullopt;
        }

        command.action = Action::New;
        command.workspaceType = DolphinDriveActions::workspaceFileType(arguments.at(2));
        urlIndex = 3;

        if (!command.workspaceType) {
            *errorMessage = i18n("Unsupported Google Workspace file type.");
            return std::nullopt;
        }
    } else {
        *errorMessage = i18n("Unknown Google Drive action: %1", verb);
        return std::nullopt;
    }

    if (arguments.size() != urlIndex + 1) {
        *errorMessage = i18n("Expected exactly one rclone URL.");
        return std::nullopt;
    }

    command.url = QUrl(arguments.at(urlIndex), QUrl::StrictMode);

    if (!command.url.isValid() || command.url.scheme() != QLatin1String("rclone")) {
        *errorMessage = i18n("The Dolphin action did not provide a valid rclone URL.");
        return std::nullopt;
    }

    return command;
}

bool openUrl(const QUrl &url, QString *errorMessage)
{
    if (QDesktopServices::openUrl(url)) {
        return true;
    }

    *errorMessage = i18n("Could not open Google Drive in the default browser.");
    return false;
}

bool copyToClipboard(const QString &text)
{
    QDBusInterface klipper(
        QStringLiteral("org.kde.klipper"),
        QStringLiteral("/klipper"),
        QStringLiteral("org.kde.klipper.klipper"),
        QDBusConnection::sessionBus());

    if (klipper.isValid()) {
        const QDBusReply<void> reply =
            klipper.call(QStringLiteral("setClipboardContents"), text);

        if (reply.isValid()) {
            return true;
        }
    }

    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText(text, QClipboard::Clipboard);
        return true;
    }

    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    KLocalizedString::setApplicationDomain("kio6_rclone");
    application.setApplicationName(QStringLiteral("kio-rclone-drive-action"));

    QString errorMessage;
    const auto command = parseCommand(application.arguments(), &errorMessage);

    if (!command) {
        if (application.arguments().size() == 1) {
            showUsage();
            return 0;
        }

        return showError(errorMessage);
    }

    const auto target = DriveActionTarget::fromUrl(command->url, &errorMessage);

    if (!target) {
        return showError(errorMessage);
    }

    CommandRcloneContext context;
    RcloneClient client;
    DriveMetadataResolver resolver(client, context);

    if (!resolver.isGoogleDriveRemote(*target, &errorMessage)) {
        return showError(errorMessage);
    }

    if (command->action == Action::Open || command->action == Action::CopyLink) {
        QUrl url;

        if (!target->itemId().isEmpty()) {
            // rclone-id comes from the worker's UDS_URL. The generic Drive
            // endpoint works for files, folders, and shortcuts without an
            // extra metadata lookup or any Google API request.
            url = DolphinDriveActions::driveOpenUrl(target->itemId());
        } else if (target->isRoot()) {
            url = target->rootFolderId().isEmpty()
                ? DolphinDriveActions::myDriveUrl()
                : DolphinDriveActions::driveFolderUrl(target->rootFolderId());
        } else if (!target->supportsPathFallback()) {
            return showError(i18n("This Google Drive view needs the item's rclone-id URL identity."));
        } else {
            const auto item = resolver.item(*target, &errorMessage);

            if (!item) {
                return showError(errorMessage);
            }

            url = command->action == Action::Open
                ? DolphinDriveActions::driveItemUrl(item->id, item->isDirectory)
                : DolphinDriveActions::driveShareUrl(item->id, item->isDirectory);
        }

        if (command->action == Action::CopyLink) {
            if (!copyToClipboard(url.toString(QUrl::FullyEncoded))) {
                return showError(i18n("Could not copy the Google Drive link to the clipboard."));
            }

            return 0;
        }

        if (!openUrl(url, &errorMessage)) {
            return showError(errorMessage);
        }

        return 0;
    }

    if (command->action == Action::New && !target->allowsCreation()) {
        return showError(i18n("New Google Workspace files can only be created in My Drive or an individual Shared Drive."));
    }

    if (command->action == Action::OpenFolder
        && !target->isRoot()
        && !target->supportsPathFallback()
        && target->itemId().isEmpty()) {
        return showError(i18n("This Google Drive view needs the item's rclone-id URL identity."));
    }

    const auto folder = command->action == Action::New && !target->itemId().isEmpty()
        // The New service menu is restricted to inode/directory. Its UDS_URL
        // therefore already identifies the destination folder.
        ? std::optional<DriveActionTarget>(*target)
        : resolver.targetFolder(*target, &errorMessage);

    if (!folder) {
        return showError(errorMessage);
    }

    const auto folderId = resolver.folderId(*folder, &errorMessage);

    if (!folderId) {
        return showError(errorMessage);
    }

    if (command->action == Action::OpenFolder) {
        const QUrl url = folderId->isEmpty()
            ? DolphinDriveActions::myDriveUrl()
            : DolphinDriveActions::driveFolderUrl(*folderId);

        if (!openUrl(url, &errorMessage)) {
            return showError(errorMessage);
        }

        return 0;
    }

    const QUrl url = DolphinDriveActions::workspaceCreateUrl(
        *command->workspaceType,
        *folderId);

    if (!openUrl(url, &errorMessage)) {
        return showError(errorMessage);
    }

    return 0;
}
