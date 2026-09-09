/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "navigation.h"
#include "url.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace {

RcloneStatus success()
{
    return {
        true,
        {RcloneErrorCode::None, {}, 0},
    };
}

RcloneStatus failure(RcloneErrorCode code,
                     const QString &message)
{
    return {
        false,
        {code, message, -1},
    };
}

QString appendPath(const QString &parent,
                   const QString &child)
{
    if (parent.isEmpty()) {
        return child;
    }

    if (child.isEmpty()) {
        return parent;
    }

    return parent + QLatin1Char('/') + child;
}

QUrl childUrlFor(const RcloneLocation &location,
                 const RcloneItem &item)
{
    const QString path =
        appendPath(location.remotePath(), item.path);

    switch (location.kind()) {
    case RcloneLocation::Kind::DriveMyDrive:
        return RcloneLocation::driveViewUrl(
            location.remoteName(),
            RcloneLocation::Kind::DriveMyDrive,
            path);

    case RcloneLocation::Kind::DriveSharedWithMe:
        return RcloneLocation::driveViewUrl(
            location.remoteName(),
            RcloneLocation::Kind::DriveSharedWithMe,
            path);

    case RcloneLocation::Kind::DriveTrash:
        return RcloneLocation::driveViewUrl(
            location.remoteName(),
            RcloneLocation::Kind::DriveTrash,
            path);

    case RcloneLocation::Kind::DriveStarred:
        return RcloneLocation::driveViewUrl(
            location.remoteName(),
            RcloneLocation::Kind::DriveStarred,
            path);

    case RcloneLocation::Kind::DriveSharedDrive:
        return RcloneLocation::driveSharedDriveUrl(
            location.remoteName(),
            location.identifier(),
            location.label(),
            path);

    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
        return RcloneLocation::standardUrl(location.remoteName(), path);
    }

    return RcloneLocation::standardUrl(location.remoteName(), path);
}

RcloneListOptions driveListOptions(const RcloneLocation &location)
{
    RcloneListOptions options;
    options.recursive = false;

    switch (location.kind()) {
    case RcloneLocation::Kind::DriveSharedWithMe:
        options.extraArguments.append(QStringLiteral("--drive-shared-with-me"));
        break;

    case RcloneLocation::Kind::DriveStarred:
        options.extraArguments.append(QStringLiteral("--drive-starred-only"));
        break;

    case RcloneLocation::Kind::DriveSharedDrive:
        options.extraArguments.append(QStringLiteral("--drive-team-drive"));
        options.extraArguments.append(location.identifier());
        break;

    case RcloneLocation::Kind::DriveMyDrive:
    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
    case RcloneLocation::Kind::DriveHub:
    case RcloneLocation::Kind::DriveSharedDrives:
    case RcloneLocation::Kind::DriveTrash:
        break;
    }

    return options;
}

bool isImmediateChildPath(const QString &path)
{
    return !path.isEmpty()
        && !path.contains(QLatin1Char('/'));
}

void markAncestorDirectoriesWithContent(const QString &path,
                                        QSet<QString> &directoriesWithContent)
{
    QString remaining = path;

    while (true) {
        const qsizetype slash = remaining.lastIndexOf(QLatin1Char('/'));

        if (slash < 0) {
            break;
        }

        remaining = remaining.left(slash);

        if (!remaining.isEmpty()) {
            directoriesWithContent.insert(remaining);
        }
    }
}

} // namespace

RcloneNavigation::RcloneNavigation(RcloneClient &client)
    : m_client(client)
{
}

RcloneStatus
RcloneNavigation::listRoot(const EntryCallback &onEntry,
                           const RcloneContext &ctx) const
{
    const auto remotes = m_client.listRemotes(ctx);

    if (!remotes.success()) {
        return {false, remotes.error};
    }

    if (!onEntry(RcloneNavigationEntry::virtualFile(
            RcloneUrl::ConfigureEntry,
            RcloneUrlBuilder::createConfigLauncher(),
            RcloneUrl::ConfigurationLauncherMimeType,
            QStringLiteral("configure")))) {
        return failure(
            RcloneErrorCode::Aborted,
            QStringLiteral("Listing aborted by consumer"));
    }

    for (const RcloneRemote &remote : *remotes.data) {
        const bool isDrive =
            remote.type == QStringLiteral("drive");

        const QUrl url = isDrive
            ? RcloneLocation::driveHubUrl(remote.name)
            : RcloneLocation::standardUrl(remote.name);

        const QString icon = isDrive
            ? QStringLiteral("folder-gdrive")
            : QStringLiteral("folder-cloud");

        if (!onEntry(RcloneNavigationEntry::virtualDirectory(
                remote.name,
                url,
                icon))) {
            return failure(
                RcloneErrorCode::Aborted,
                QStringLiteral("Listing aborted by consumer"));
        }
    }

    return success();
}

RcloneStatus
RcloneNavigation::list(const RcloneLocation &location,
                       const EntryCallback &onEntry,
                       const RcloneContext &ctx) const
{
    if (location.isRoot()) {
        return listRoot(onEntry, ctx);
    }

    if (location.isConfigureEntry()) {
        return failure(
            RcloneErrorCode::Unsupported,
            QStringLiteral("Configuration entry is not a directory"));
    }

    if (location.kind() == RcloneLocation::Kind::Standard) {
        const auto type = remoteType(location.remoteName(), ctx);

        if (!type.success()) {
            return {false, type.error};
        }

        if (*type.data == QStringLiteral("drive")
            && location.remotePath().isEmpty()) {
            return listDriveHub(location, onEntry);
        }

        return listStandard(location, onEntry, ctx);
    }

    if (location.kind() == RcloneLocation::Kind::DriveHub) {
        return listDriveHub(location, onEntry);
    }

    if (location.kind() == RcloneLocation::Kind::DriveSharedDrives) {
        return listSharedDrives(location, onEntry, ctx);
    }

    if (location.kind() == RcloneLocation::Kind::DriveTrash) {
        return listDriveTrash(location, onEntry, ctx);
    }

    return listDriveView(location, onEntry, ctx);
}

RcloneResponse<QString>
RcloneNavigation::remoteType(const QString &remoteName,
                             const RcloneContext &ctx) const
{
    const auto remotes = m_client.listRemotes(ctx);

    if (!remotes.success()) {
        return {std::nullopt, remotes.error};
    }

    for (const RcloneRemote &remote : *remotes.data) {
        if (remote.name == remoteName) {
            return {
                remote.type,
                {RcloneErrorCode::None, {}, 0},
            };
        }
    }

    return {
        std::nullopt,
        {
            RcloneErrorCode::NotFound,
            i18n("Remote %1 was not found", remoteName),
            -1,
        },
    };
}

RcloneStatus
RcloneNavigation::listStandard(const RcloneLocation &location,
                               const EntryCallback &onEntry,
                               const RcloneContext &ctx) const
{
    return m_client.list(
        location.toCliSpec(),
        false,
        [&](const RcloneItem &item) {
            return onEntry(RcloneNavigationEntry::fromItem(
                item,
                childUrlFor(location, item)));
        },
        ctx);
}

RcloneStatus
RcloneNavigation::listDriveHub(const RcloneLocation &location,
                               const EntryCallback &onEntry) const
{
    const QString remoteName = location.remoteName();

    const QList<RcloneNavigationEntry> entries{
        RcloneNavigationEntry::virtualDirectory(
            i18n("My Drive"),
            RcloneLocation::driveViewUrl(
                remoteName,
                RcloneLocation::Kind::DriveMyDrive),
            QStringLiteral("user-home")),

        RcloneNavigationEntry::virtualDirectory(
            i18n("Shared With Me"),
            RcloneLocation::driveViewUrl(
                remoteName,
                RcloneLocation::Kind::DriveSharedWithMe),
            QStringLiteral("folder-publicshare")),

        RcloneNavigationEntry::virtualDirectory(
            i18n("Shared Drives"),
            RcloneLocation::driveViewUrl(
                remoteName,
                RcloneLocation::Kind::DriveSharedDrives),
            QStringLiteral("folder-cloud")),

        RcloneNavigationEntry::virtualDirectory(
            i18n("Trash"),
            RcloneLocation::driveViewUrl(
                remoteName,
                RcloneLocation::Kind::DriveTrash),
            QStringLiteral("user-trash-full")),

        RcloneNavigationEntry::virtualDirectory(
            i18n("Starred"),
            RcloneLocation::driveViewUrl(
                remoteName,
                RcloneLocation::Kind::DriveStarred),
            QStringLiteral("folder-favorites")),
    };

    for (const RcloneNavigationEntry &entry : entries) {
        if (!onEntry(entry)) {
            return failure(
                RcloneErrorCode::Aborted,
                QStringLiteral("Listing aborted by consumer"));
        }
    }

    return success();
}

RcloneStatus
RcloneNavigation::listDriveView(const RcloneLocation &location,
                                const EntryCallback &onEntry,
                                const RcloneContext &ctx) const
{
    return m_client.list(
        location.toCliSpec(),
        driveListOptions(location),
        [&](const RcloneItem &item) {
            return onEntry(RcloneNavigationEntry::fromItem(
                item,
                childUrlFor(location, item)));
        },
        ctx);
}

RcloneStatus
RcloneNavigation::listSharedDrives(const RcloneLocation &location,
                                   const EntryCallback &onEntry,
                                   const RcloneContext &ctx) const
{
    const QString remoteSpec =
        location.remoteName() + QLatin1Char(':');

    const auto result = m_client.backendQuery(
        remoteSpec,
        QStringLiteral("drives"),
        {},
        ctx);

    if (!result.success()) {
        return {false, result.error};
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(*result.data, &parseError);

    if (parseError.error != QJsonParseError::NoError
        || !document.isArray()) {
        return failure(
            RcloneErrorCode::InvalidResponse,
            i18n("Invalid shared drives JSON: %1",
                 parseError.errorString()));
    }

    const QJsonArray array = document.array();

    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            return failure(
                RcloneErrorCode::InvalidResponse,
                i18n("Invalid shared drive entry at index %1", i));
        }

        const auto drive =
            RcloneSharedDrive::fromJson(array.at(i).toObject());

        if (!drive) {
            return failure(
                RcloneErrorCode::InvalidResponse,
                i18n("Invalid shared drive entry at index %1", i));
        }

        if (!onEntry(RcloneNavigationEntry::virtualDirectory(
                drive->name,
                RcloneLocation::driveSharedDriveUrl(
                    location.remoteName(),
                    drive->id,
                    drive->name),
                QStringLiteral("folder-cloud")))) {
            return failure(
                RcloneErrorCode::Aborted,
                QStringLiteral("Listing aborted by consumer"));
        }
    }

    return success();
}

RcloneStatus
RcloneNavigation::listDriveTrash(const RcloneLocation &location,
                                 const EntryCallback &onEntry,
                                 const RcloneContext &ctx) const
{
    RcloneListOptions options;
    options.recursive = true;
    options.extraArguments.append(QStringLiteral("--drive-trashed-only"));

    QList<RcloneItem> items;

    const RcloneStatus status = m_client.list(
        location.toCliSpec(),
        options,
        [&](const RcloneItem &item) {
            items.append(item);
            return true;
        },
        ctx);

    if (!status.success()) {
        return status;
    }

    /*
     * Carpetas que tienen contenido real debajo.
     *
     * Marcamos ancestros solamente a partir de archivos. Así una carpeta
     * que aparece en Trash pero no contiene archivos listados se oculta.
     */
    QSet<QString> directoriesWithContent;

    for (const RcloneItem &item : items) {
        if (item.isDirectory) {
            continue;
        }

        markAncestorDirectoriesWithContent(
            item.path,
            directoriesWithContent);
    }

    /*
     * Aunque el listado interno fue recursivo, listDir() solo debe emitir
     * hijos inmediatos de la ubicación actual.
     */
    for (const RcloneItem &item : items) {
        if (!isImmediateChildPath(item.path)) {
            continue;
        }

        if (item.isDirectory
            && !directoriesWithContent.contains(item.path)) {
            continue;
        }

        if (!onEntry(RcloneNavigationEntry::fromItem(
                item,
                childUrlFor(location, item)))) {
            return failure(
                RcloneErrorCode::Aborted,
                QStringLiteral("Listing aborted by consumer"));
        }
    }

    return success();
}
