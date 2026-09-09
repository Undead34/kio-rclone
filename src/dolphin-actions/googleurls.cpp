/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "googleurls.h"

#include <QUrlQuery>

namespace {

QUrl makeUrl(const QString &host, const QString &path)
{
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(host);
    url.setPath(path);
    return url;
}

QString workspacePath(DolphinDriveActions::WorkspaceFileType type)
{
    using DolphinDriveActions::WorkspaceFileType;

    switch (type) {
    case WorkspaceFileType::Document:
        return QStringLiteral("/document/create");
    case WorkspaceFileType::Spreadsheet:
        return QStringLiteral("/spreadsheets/create");
    case WorkspaceFileType::Presentation:
        return QStringLiteral("/presentation/create");
    case WorkspaceFileType::Drawing:
        return QStringLiteral("/drawings/create");
    }

    return {};
}

} // namespace

namespace DolphinDriveActions
{

std::optional<WorkspaceFileType>
workspaceFileType(const QString &argument)
{
    if (argument == QLatin1String("doc")) {
        return WorkspaceFileType::Document;
    }

    if (argument == QLatin1String("sheet")) {
        return WorkspaceFileType::Spreadsheet;
    }

    if (argument == QLatin1String("slide")) {
        return WorkspaceFileType::Presentation;
    }

    if (argument == QLatin1String("drawing")) {
        return WorkspaceFileType::Drawing;
    }

    return std::nullopt;
}

QUrl myDriveUrl()
{
    return makeUrl(
        QStringLiteral("drive.google.com"),
        QStringLiteral("/drive/my-drive"));
}

QUrl driveFolderUrl(const QString &folderId)
{
    return makeUrl(
        QStringLiteral("drive.google.com"),
        QStringLiteral("/drive/folders/") + folderId);
}

QUrl driveOpenUrl(const QString &itemId)
{
    QUrl url = makeUrl(
        QStringLiteral("drive.google.com"),
        QStringLiteral("/open"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), itemId);
    url.setQuery(query);
    return url;
}

QUrl driveItemUrl(const QString &itemId, bool isDirectory)
{
    if (isDirectory) {
        return driveFolderUrl(itemId);
    }

    return makeUrl(
        QStringLiteral("drive.google.com"),
        QStringLiteral("/file/d/") + itemId + QStringLiteral("/view"));
}

QUrl driveShareUrl(const QString &itemId, bool isDirectory)
{
    if (isDirectory) {
        return driveFolderUrl(itemId);
    }

    return driveOpenUrl(itemId);
}

QUrl workspaceCreateUrl(WorkspaceFileType type, const QString &folderId)
{
    QUrl url = makeUrl(QStringLiteral("docs.google.com"), workspacePath(type));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("usp"), QStringLiteral("drive_web"));

    if (!folderId.isEmpty()) {
        query.addQueryItem(QStringLiteral("folder"), folderId);
    }

    url.setQuery(query);
    return url;
}

} // namespace DolphinDriveActions
