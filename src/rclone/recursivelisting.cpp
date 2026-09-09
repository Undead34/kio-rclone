/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "recursivelisting.h"

#include "entryformat.h"

#include <QChar>
#include <QStringList>

#include <utility>

namespace
{
void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool isSafePathComponent(const QString &component)
{
    return !component.isEmpty() && component != QLatin1String(".") && component != QLatin1String("..")
        && !component.contains(QChar::Null);
}

std::optional<QStringList> componentsFor(const RcloneItem &item)
{
    if (item.path.isEmpty() || item.path.startsWith(QLatin1Char('/')) || item.name.isEmpty() || item.name.contains(QLatin1Char('/'))
        || item.name == QLatin1String(".") || item.name == QLatin1String("..") || item.name.contains(QChar::Null)) {
        return std::nullopt;
    }

    const QStringList components = item.path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (components.isEmpty() || components.constLast() != item.name) {
        return std::nullopt;
    }
    for (const QString &component : components) {
        if (!isSafePathComponent(component)) {
            return std::nullopt;
        }
    }
    return components;
}

QString parentPath(const QString &path)
{
    return path.section(QLatin1Char('/'), 0, -2);
}

RcloneItem syntheticDirectory(const QString &name)
{
    RcloneItem item;
    item.name = name;
    item.path = name;
    item.mimeType = QStringLiteral("inode/directory");
    item.size = -1;
    item.isDirectory = true;
    item.readOnly = true;
    item.syntheticDirectory = true;
    return item;
}

bool hasDescendant(const QMap<QString, RcloneItem> &nodes, QMap<QString, RcloneItem>::const_iterator current)
{
    const QString prefix = current.key() + QLatin1Char('/');
    const auto next = std::next(current);
    return next != nodes.cend() && next.key().startsWith(prefix);
}
} // namespace

RcloneRecursiveListing::RcloneRecursiveListing(Directories directories)
    : m_directories(std::move(directories))
{
}

std::optional<RcloneRecursiveListing> RcloneRecursiveListing::fromItems(const QList<RcloneItem> &recursiveItems, QString *error)
{
    QMap<QString, RcloneItem> nodes;

    for (const RcloneItem &rawItem : recursiveItems) {
        const std::optional<QStringList> components = componentsFor(rawItem);
        if (!components) {
            // Match the normal KIO listing path: one remote name that cannot
            // become a URL component must not make its siblings disappear.
            continue;
        }

        QString path;
        for (qsizetype index = 0; index + 1 < components->size(); ++index) {
            path += (path.isEmpty() ? QString() : QStringLiteral("/")) + components->at(index);
            if (!nodes.contains(path)) {
                nodes.insert(path, syntheticDirectory(components->at(index)));
            }
        }

        const QString itemPath = components->join(QLatin1Char('/'));
        RcloneItem item = rawItem;
        item.name = components->constLast();
        item.path = item.name;
        item.syntheticDirectory = false;

        const auto existing = nodes.constFind(itemPath);
        if (existing == nodes.cend()) {
            nodes.insert(itemPath, std::move(item));
            continue;
        }

        if (existing->syntheticDirectory) {
            nodes[itemPath] = std::move(item);
            continue;
        }

        RcloneItem selected = *existing;
        if (RcloneEntryFormat::preferItem(item, selected)) {
            selected = std::move(item);
        }
        selected.ambiguous = true;
        selected.readOnly = true;
        nodes[itemPath] = std::move(selected);
    }

    for (auto node = nodes.cbegin(); node != nodes.cend(); ++node) {
        if (!node->isDirectory && hasDescendant(nodes, node)) {
            setError(error, QStringLiteral("rclone returned a file that is also a parent path"));
            return std::nullopt;
        }
    }

    Directories directories;
    directories.insert({}, {});
    for (auto node = nodes.cbegin(); node != nodes.cend(); ++node) {
        const QString directory = parentPath(node.key());
        directories[directory].append(node.value());
        if (node->isDirectory && !directories.contains(node.key())) {
            directories.insert(node.key(), {});
        }
    }

    // Podar directorios que quedaron vacíos (bottom-up)
        QList<QString> dirPaths = directories.keys();
        // Ordenar de mayor a menor longitud para procesar las hojas primero
        std::sort(dirPaths.begin(), dirPaths.end(), [](const QString &a, const QString &b) {
            return a.length() > b.length();
        });

        for (const QString &path : dirPaths) {
            if (path.isEmpty()) {
                continue; // Nunca podar la raíz
            }
            if (directories.value(path).isEmpty()) {
                const QString parent = parentPath(path);
                const QString dirName = path.section(QLatin1Char('/'), -1);

                // Eliminar este directorio de la lista de su padre
                if (directories.contains(parent)) {
                    auto &parentEntries = directories[parent];
                    parentEntries.erase(
                        std::remove_if(parentEntries.begin(), parentEntries.end(),
                                       [&dirName](const RcloneItem &item) { return item.name == dirName; }),
                        parentEntries.end());
                }
                // Eliminar la entrada del mapa
                directories.remove(path);
            }
        }

    return RcloneRecursiveListing(std::move(directories));
}

std::optional<QList<RcloneItem>> RcloneRecursiveListing::entries(const QString &relativePath) const
{
    const auto directory = m_directories.constFind(relativePath);
    if (directory == m_directories.cend()) {
        return std::nullopt;
    }
    return *directory;
}

const RcloneRecursiveListing::Directories &RcloneRecursiveListing::directories() const
{
    return m_directories;
}
