/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "location.h"

#include <QUrlQuery>
#include <utility>

namespace {

constexpr auto ViewQueryKey = "rclone-view";
constexpr auto IdQueryKey = "id";
constexpr auto NameQueryKey = "name";

QString viewName(RcloneLocation::Kind kind)
{
    switch (kind) {
    case RcloneLocation::Kind::DriveHub:
        return QStringLiteral("drive-hub");
    case RcloneLocation::Kind::DriveMyDrive:
        return QStringLiteral("drive-my-drive");
    case RcloneLocation::Kind::DriveSharedWithMe:
        return QStringLiteral("drive-shared-with-me");
    case RcloneLocation::Kind::DriveSharedDrives:
        return QStringLiteral("drive-shared-drives");
    case RcloneLocation::Kind::DriveTrash:
        return QStringLiteral("drive-trash");
    case RcloneLocation::Kind::DriveStarred:
        return QStringLiteral("drive-starred");
    case RcloneLocation::Kind::DriveSharedDrive:
        return QStringLiteral("drive-shared-drive");
    case RcloneLocation::Kind::Root:
    case RcloneLocation::Kind::ConfigureEntry:
    case RcloneLocation::Kind::Standard:
        break;
    }

    return {};
}

std::optional<RcloneLocation::Kind> kindFromView(const QString &view)
{
    if (view == QStringLiteral("drive-hub")) {
        return RcloneLocation::Kind::DriveHub;
    }

    if (view == QStringLiteral("drive-my-drive")) {
        return RcloneLocation::Kind::DriveMyDrive;
    }

    if (view == QStringLiteral("drive-shared-with-me")) {
        return RcloneLocation::Kind::DriveSharedWithMe;
    }

    if (view == QStringLiteral("drive-shared-drives")) {
        return RcloneLocation::Kind::DriveSharedDrives;
    }

    if (view == QStringLiteral("drive-trash")) {
        return RcloneLocation::Kind::DriveTrash;
    }

    if (view == QStringLiteral("drive-starred")) {
        return RcloneLocation::Kind::DriveStarred;
    }

    if (view == QStringLiteral("drive-shared-drive")) {
        return RcloneLocation::Kind::DriveSharedDrive;
    }

    return std::nullopt;
}

QUrl makeBaseUrl(const QString &remoteName,
                 const QString &remotePath)
{
    QString path = QLatin1Char('/') + remoteName;

    if (!remotePath.isEmpty()) {
        path += QLatin1Char('/');
        path += remotePath;
    } else {
        path += QLatin1Char('/');
    }

    QUrl url;
    url.setScheme(QStringLiteral("rclone"));
    url.setPath(path);

    return url;
}

QUrl withDriveView(QUrl url,
                   RcloneLocation::Kind kind,
                   const QString &identifier = {},
                   const QString &label = {})
{
    QUrlQuery query;
    query.addQueryItem(QString::fromLatin1(ViewQueryKey), viewName(kind));

    if (!identifier.isEmpty()) {
        query.addQueryItem(QString::fromLatin1(IdQueryKey), identifier);
    }

    if (!label.isEmpty()) {
        query.addQueryItem(QString::fromLatin1(NameQueryKey), label);
    }

    url.setQuery(query);
    return url;
}

} // namespace

RcloneLocation::RcloneLocation(RcloneUrl base,
                               Kind kind,
                               QString identifier,
                               QString label)
    : m_base(std::move(base))
    , m_kind(kind)
    , m_identifier(std::move(identifier))
    , m_label(std::move(label))
{
}

std::optional<RcloneLocation>
RcloneLocation::parse(const QUrl &url)
{
    QUrl baseUrl = url;
    const QUrlQuery query(url);

    baseUrl.setQuery(QString{});

    auto base = RcloneUrl::parse(baseUrl);

    if (!base) {
        return std::nullopt;
    }

    if (base->isRoot()) {
        return RcloneLocation(std::move(*base), Kind::Root);
    }

    if (base->isConfigureEntry()) {
        return RcloneLocation(std::move(*base), Kind::ConfigureEntry);
    }

    const QString view =
        query.queryItemValue(QString::fromLatin1(ViewQueryKey));

    if (view.isEmpty()) {
        return RcloneLocation(std::move(*base), Kind::Standard);
    }

    const auto kind = kindFromView(view);

    if (!kind) {
        return std::nullopt;
    }

    return RcloneLocation(
        std::move(*base),
        *kind,
        query.queryItemValue(QString::fromLatin1(IdQueryKey)),
        query.queryItemValue(QString::fromLatin1(NameQueryKey)));
}

QUrl RcloneLocation::standardUrl(const QString &remoteName,
                                 const QString &remotePath)
{
    return makeBaseUrl(remoteName, remotePath);
}

QUrl RcloneLocation::driveHubUrl(const QString &remoteName)
{
    return withDriveView(
        makeBaseUrl(remoteName, {}),
        Kind::DriveHub);
}

QUrl RcloneLocation::driveViewUrl(const QString &remoteName,
                                  Kind kind,
                                  const QString &remotePath)
{
    return withDriveView(
        makeBaseUrl(remoteName, remotePath),
        kind);
}

QUrl RcloneLocation::driveSharedDriveUrl(const QString &remoteName,
                                         const QString &driveId,
                                         const QString &driveName,
                                         const QString &remotePath)
{
    return withDriveView(
        makeBaseUrl(remoteName, remotePath),
        Kind::DriveSharedDrive,
        driveId,
        driveName);
}

RcloneLocation::Kind RcloneLocation::kind() const
{
    return m_kind;
}

bool RcloneLocation::isRoot() const
{
    return m_kind == Kind::Root;
}

bool RcloneLocation::isConfigureEntry() const
{
    return m_kind == Kind::ConfigureEntry;
}

bool RcloneLocation::isDriveVirtual() const
{
    switch (m_kind) {
    case Kind::DriveHub:
    case Kind::DriveMyDrive:
    case Kind::DriveSharedWithMe:
    case Kind::DriveSharedDrives:
    case Kind::DriveTrash:
    case Kind::DriveStarred:
    case Kind::DriveSharedDrive:
        return true;
    case Kind::Root:
    case Kind::ConfigureEntry:
    case Kind::Standard:
        return false;
    }

    return false;
}

QString RcloneLocation::remoteName() const
{
    return m_base.remoteName();
}

QString RcloneLocation::remotePath() const
{
    return m_base.remotePath();
}

QString RcloneLocation::toCliSpec() const
{
    return m_base.toCliSpec();
}

QUrl RcloneLocation::toQUrl() const
{
    return m_base.toQUrl();
}

QString RcloneLocation::identifier() const
{
    return m_identifier;
}

QString RcloneLocation::label() const
{
    return m_label;
}
