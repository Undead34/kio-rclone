/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drivemetadataresolver.h"

#include <KLocalizedString>

namespace {

void setError(QString *destination, const QString &message)
{
    if (destination) {
        *destination = message;
    }
}

QString responseMessage(const RcloneError &error,
                        const QString &fallback)
{
    return error.message.isEmpty() ? fallback : error.message;
}

} // namespace

namespace DolphinDriveActions
{

DriveMetadataResolver::DriveMetadataResolver(RcloneClient &client,
                                             const RcloneContext &context)
    : m_client(client)
    , m_context(context)
{
}

bool DriveMetadataResolver::isGoogleDriveRemote(
    const DriveActionTarget &target,
    QString *errorMessage) const
{
    const auto remotes = m_client.listRemotes(m_context);

    if (!remotes.success()) {
        setError(
            errorMessage,
            responseMessage(
                remotes.error,
                i18n("Could not read the rclone configuration.")));
        return false;
    }

    for (const RcloneRemote &remote : *remotes.data) {
        if (remote.name != target.remoteName()) {
            continue;
        }

        if (remote.type == QLatin1String("drive")) {
            return true;
        }

        setError(
            errorMessage,
            i18n("“%1” is not a Google Drive remote.", target.remoteName()));
        return false;
    }

    setError(
        errorMessage,
        i18n("The rclone remote “%1” no longer exists.", target.remoteName()));
    return false;
}

std::optional<RcloneItem>
DriveMetadataResolver::item(const DriveActionTarget &target,
                            QString *errorMessage) const
{
    if (target.isRoot()) {
        RcloneItem root;
        root.id = target.rootFolderId();
        root.isDirectory = true;
        root.mimeType = QStringLiteral("inode/directory");
        return root;
    }

    std::optional<RcloneResponse<RcloneItem>> stat;

    if (target.supportsPathFallback()) {
        stat = m_client.stat(target.statSpec(), m_context);

        if (stat->success()) {
            if (!stat->data->id.isEmpty()) {
                return *stat->data;
            }

            if (!stat->data->isDirectory) {
                setError(
                    errorMessage,
                    i18n("Google Drive did not return an identifier for the selected file."));
                return std::nullopt;
            }
        }
    }

    QString listingError;
    const auto listed = itemFromParentListing(target, &listingError);

    if (listed) {
        return listed;
    }

    if (!listingError.isEmpty()) {
        setError(errorMessage, listingError);
    } else if (stat && !stat->success()) {
        setError(
            errorMessage,
            responseMessage(
                stat->error,
                i18n("Could not resolve Google Drive metadata.")));
    } else {
        setError(
            errorMessage,
            i18n("Could not resolve Google Drive metadata."));
    }

    return std::nullopt;
}

std::optional<DriveActionTarget>
DriveMetadataResolver::targetFolder(const DriveActionTarget &target,
                                    QString *errorMessage) const
{
    const auto resolvedItem = item(target, errorMessage);

    if (!resolvedItem) {
        return std::nullopt;
    }

    return resolvedItem->isDirectory ? target : target.parent();
}

std::optional<QString>
DriveMetadataResolver::folderId(const DriveActionTarget &target,
                                QString *errorMessage) const
{
    if (!target.itemId().isEmpty()) {
        return target.itemId();
    }

    if (target.isRoot()) {
        return target.rootFolderId();
    }

    const auto resolvedItem = item(target, errorMessage);

    if (!resolvedItem) {
        return std::nullopt;
    }

    if (!resolvedItem->isDirectory) {
        setError(errorMessage, i18n("The Google Drive target is not a folder."));
        return std::nullopt;
    }

    if (resolvedItem->id.isEmpty()) {
        setError(
            errorMessage,
            i18n("Google Drive did not return an identifier for the selected folder."));
        return std::nullopt;
    }

    return resolvedItem->id;
}

std::optional<RcloneItem>
DriveMetadataResolver::itemFromParentListing(
    const DriveActionTarget &target,
    QString *errorMessage) const
{
    const DriveActionTarget parent = target.parent();
    RcloneListOptions options;
    options.extraArguments = parent.listingArguments();

    QList<RcloneItem> matches;
    const RcloneStatus status = m_client.list(
        parent.listingSpec(),
        options,
        [&](const RcloneItem &candidate) {
            if (candidate.name == target.leafName()) {
                matches.append(candidate);
            }

            return true;
        },
        m_context);

    if (!status.success()) {
        setError(
            errorMessage,
            responseMessage(
                status.error,
                i18n("Could not list the containing Google Drive folder.")));
        return std::nullopt;
    }

    if (matches.isEmpty()) {
        setError(
            errorMessage,
            i18n("The selected item was not found in its Google Drive folder."));
        return std::nullopt;
    }

    if (matches.size() != 1) {
        setError(
            errorMessage,
            i18n("The selected item has an ambiguous name in Google Drive."));
        return std::nullopt;
    }

    if (matches.constFirst().id.isEmpty()) {
        setError(
            errorMessage,
            i18n("Google Drive did not return an identifier for the selected item."));
        return std::nullopt;
    }

    return matches.constFirst();
}

} // namespace DolphinDriveActions
