/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QUrl>

#include <optional>

namespace DolphinDriveActions
{

enum class WorkspaceFileType {
    Document,
    Spreadsheet,
    Presentation,
    Drawing,
};

[[nodiscard]] std::optional<WorkspaceFileType>
workspaceFileType(const QString &argument);

[[nodiscard]] QUrl myDriveUrl();
[[nodiscard]] QUrl driveFolderUrl(const QString &folderId);
[[nodiscard]] QUrl driveOpenUrl(const QString &itemId);
[[nodiscard]] QUrl driveItemUrl(const QString &itemId, bool isDirectory);
[[nodiscard]] QUrl driveShareUrl(const QString &itemId, bool isDirectory);
[[nodiscard]] QUrl workspaceCreateUrl(WorkspaceFileType type,
                                       const QString &folderId = {});

} // namespace DolphinDriveActions
