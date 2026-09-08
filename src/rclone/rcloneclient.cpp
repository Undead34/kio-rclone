/*
 * SPDX-FileCopyrightText: 2026 Gabriel Maizo González <maizogabriel@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rcloneclient.h"
#include "process.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace
{
constexpr qsizetype MaximumListingItemSize = 4 * 1024 * 1024;

std::optional<RcloneItem> itemFromObject(const QJsonObject &object)
{
    if (object.isEmpty()) {
        return std::nullopt;
    }

    RcloneItem item;
    item.name = object.value(QStringLiteral("Name")).toString();
    item.path = object.value(QStringLiteral("Path")).toString();
    item.id = object.value(QStringLiteral("ID")).toString();
    item.mimeType = object.value(QStringLiteral("MimeType")).toString();
    item.size = object.value(QStringLiteral("Size")).toVariant().toLongLong();
    item.isDirectory = object.value(QStringLiteral("IsDir")).toBool();
    item.readOnly = !item.isDirectory && item.size < 0;
    item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODateWithMs);
    if (!item.modificationTime.isValid()) {
        item.modificationTime = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODate);
    }

    if (item.name.isEmpty() && !item.path.isEmpty()) {
        item.name = item.path.section(QLatin1Char('/'), -1);
    }
    return item;
}

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool isJsonWhitespace(char value)
{
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

// lsjson writes one JSON object at a time, but its output is still a valid JSON
// array.  Parse the array incrementally instead of assuming a line layout: a
// filename may legally contain escaped newlines or braces.
class JsonArrayItemReader
{
  public:
    explicit JsonArrayItemReader(RcloneClient::ItemCallback onItem)
        : m_onItem(std::move(onItem))
    {
    }

    bool feed(const QByteArray &data, QString *error)
    {
        for (const char character : data) {
            if (!consume(character, error)) {
                return false;
            }
        }
        return true;
    }

    bool finish(QString *error) const
    {
        if (m_state == State::Finished) {
            return true;
        }
        setError(error, QStringLiteral("rclone returned an incomplete JSON listing"));
        return false;
    }

  private:
    enum class State {
        BeforeArray,
        BeforeItem,
        InItem,
        AfterItem,
        Finished,
    };

    bool fail(QString *error, const QString &message) const
    {
        setError(error, message);
        return false;
    }

    bool completeItem(QString *error)
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_item, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return fail(error, QStringLiteral("Invalid item in rclone listing: %1").arg(parseError.errorString()));
        }

        const std::optional<RcloneItem> item = itemFromObject(document.object());
        if (!item) {
            return fail(error, QStringLiteral("rclone returned an empty listing item"));
        }
        if (!m_onItem(*item)) {
            return fail(error, QStringLiteral("Listing was stopped by the caller"));
        }

        m_item.clear();
        m_state = State::AfterItem;
        return true;
    }

    bool consume(char character, QString *error)
    {
        switch (m_state) {
        case State::BeforeArray:
            if (isJsonWhitespace(character)) {
                return true;
            }
            if (character != '[') {
                return fail(error, QStringLiteral("rclone listing is not a JSON array"));
            }
            m_state = State::BeforeItem;
            m_allowArrayEnd = true;
            return true;

        case State::BeforeItem:
            if (isJsonWhitespace(character)) {
                return true;
            }
            if (character == ']') {
                return m_allowArrayEnd ? finishArray() : fail(error, QStringLiteral("Unexpected end of rclone listing"));
            }
            if (character != '{') {
                return fail(error, QStringLiteral("rclone listing contains a non-object item"));
            }
            m_item = QByteArray(1, character);
            m_objectDepth = 1;
            m_inString = false;
            m_escaped = false;
            m_state = State::InItem;
            return true;

        case State::InItem:
            if (m_item.size() >= MaximumListingItemSize) {
                return fail(error, QStringLiteral("A single rclone listing item is too large to process safely"));
            }
            m_item.append(character);
            if (m_inString) {
                if (m_escaped) {
                    m_escaped = false;
                } else if (character == '\\') {
                    m_escaped = true;
                } else if (character == '"') {
                    m_inString = false;
                }
                return true;
            }

            if (character == '"') {
                m_inString = true;
            } else if (character == '{') {
                ++m_objectDepth;
            } else if (character == '}') {
                --m_objectDepth;
                if (m_objectDepth < 0) {
                    return fail(error, QStringLiteral("Invalid object nesting in rclone listing"));
                }
                if (m_objectDepth == 0) {
                    return completeItem(error);
                }
            }
            return true;

        case State::AfterItem:
            if (isJsonWhitespace(character)) {
                return true;
            }
            if (character == ',') {
                m_state = State::BeforeItem;
                m_allowArrayEnd = false;
                return true;
            }
            if (character == ']') {
                return finishArray();
            }
            return fail(error, QStringLiteral("Missing separator in rclone listing"));

        case State::Finished:
            return isJsonWhitespace(character) ? true : fail(error, QStringLiteral("Unexpected data after rclone listing"));
        }

        return fail(error, QStringLiteral("Invalid rclone listing parser state"));
    }

    bool finishArray()
    {
        m_state = State::Finished;
        return true;
    }

    RcloneClient::ItemCallback m_onItem;
    State m_state = State::BeforeArray;
    QByteArray m_item;
    int m_objectDepth = 0;
    bool m_inString = false;
    bool m_escaped = false;
    bool m_allowArrayEnd = false;
};
} // namespace

bool RcloneResult::success() const
{
    return started && !cancelled && !timedOut && exitCode == 0;
}

QString RcloneResult::errorMessage() const
{
    const QString stderrMessage = QString::fromUtf8(standardError).trimmed();
    if (!stderrMessage.isEmpty()) {
        return stderrMessage;
    }

    const QString stdoutMessage = QString::fromUtf8(standardOutput).trimmed();
    if (!stdoutMessage.isEmpty()) {
        return stdoutMessage;
    }
    if (!started) {
        return QStringLiteral("rclone could not be started");
    }
    if (timedOut) {
        return QStringLiteral("rclone timed out");
    }
    return QStringLiteral("rclone exited with code %1").arg(exitCode);
}

RcloneClient::RcloneClient(QString executable)
    : m_executable(executable.isEmpty() ? locateExecutable() : std::move(executable))
{
}

QString RcloneClient::executable() const
{
    return m_executable;
}

bool RcloneClient::isAvailable() const
{
    return !m_executable.isEmpty() && QFileInfo(m_executable).isExecutable();
}

RcloneResult RcloneClient::run(const QStringList &arguments, int timeoutMs, const CancellationCallback &isCancelled) const
{
    RcloneResult result;
    if (!isAvailable()) {
        return result;
    }

    QProcess process;
    RcloneProcess::configureProcess(process, m_executable, arguments);
    process.start();
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            RcloneProcess::stopProcess(process);
            break;
        }
        if (timeoutMs >= 0 && timer.elapsed() >= timeoutMs) {
            result.timedOut = true;
            RcloneProcess::stopProcess(process);
            break;
        }
        process.waitForFinished(100);
    }

    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    result.exitCode = process.exitCode();
    return result;
}

QList<RcloneRemote> RcloneClient::remoteList(QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("listremotes"), QStringLiteral("--json")}, 30000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return {};
    }

    return parseRemoteListWithTypes(result.standardOutput, error);
}

QStringList RcloneClient::remotes(QString *error, const CancellationCallback &isCancelled) const
{
    const QList<RcloneRemote> remoteListResult = remoteList(error, isCancelled);

    QStringList result;
    result.reserve(remoteListResult.size());
    for (const RcloneRemote &remote : remoteListResult) {
        result.append(remote.name);
    }
    return result;
}

std::optional<RcloneRemoteInfo> RcloneClient::remoteInfo(const QString &remote, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("config"), QStringLiteral("redacted"), remote}, 30000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return std::nullopt;
    }
    return parseRemoteInfo(result.standardOutput, error);
}

std::optional<bool>
RcloneClient::mayHaveDuplicateNames(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("backend"), QStringLiteral("features"), remoteSpec, QStringLiteral("--json")}, 30000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return std::nullopt;
    }
    return parseDuplicateNameSupport(result.standardOutput, error);
}

bool RcloneClient::listStreaming(const QString &remoteSpec,
                                  const ItemCallback &onItem,
                                  QString *error,
                                  const CancellationCallback &isCancelled) const
{
    if (!isAvailable()) {
        setError(error, QStringLiteral("rclone could not be started"));
        return false;
    }
    if (!onItem) {
        setError(error, QStringLiteral("No callback was provided for the rclone listing"));
        return false;
    }

    RcloneResult result;
    QProcess process;
    RcloneProcess::configureProcess(process, m_executable, {QStringLiteral("lsjson"), remoteSpec});
    process.start();
    result.started = process.waitForStarted(5000);
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        setError(error, result.errorMessage());
        return false;
    }

    QString parsingError;
    JsonArrayItemReader reader(onItem);
    const auto consumeOutput = [&process, &result, &reader, &parsingError]() {
        const QByteArray output = process.readAllStandardOutput();
        RcloneProcess::appendLimited(result.standardError, process.readAllStandardError());
        return output.isEmpty() || reader.feed(output, &parsingError);
    };

    QElapsedTimer timer;
    timer.start();
    bool parsingStopped = false;
    while (process.state() != QProcess::NotRunning) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            RcloneProcess::stopProcess(process);
            break;
        }
        if (timer.elapsed() >= 120000) {
            result.timedOut = true;
            RcloneProcess::stopProcess(process);
            break;
        }

        process.waitForReadyRead(100);
        if (!consumeOutput()) {
            parsingStopped = true;
            RcloneProcess::stopProcess(process);
            break;
        }
    }

    if (!parsingStopped && !result.cancelled && !result.timedOut && !consumeOutput()) {
        parsingStopped = true;
    }
    RcloneProcess::appendLimited(result.standardError, process.readAllStandardError());
    result.exitCode = process.exitCode();

    if (result.cancelled) {
        return false;
    }
    if (result.timedOut) {
        setError(error, result.errorMessage());
        return false;
    }
    if (parsingStopped) {
        setError(error, parsingError);
        return false;
    }
    if (!result.success()) {
        setError(error, result.errorMessage());
        return false;
    }
    return reader.finish(error);
}

QList<RcloneRemote> RcloneClient::parseRemoteListWithTypes(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(error, parseError.errorString());
        return {};
    }

    QList<RcloneRemote> remotes;
    for (const QJsonValue &value : document.array()) {
        RcloneRemote remote;
        if (value.isString()) {
            remote.name = value.toString();
        } else if (value.isObject()) {
            const QJsonObject object = value.toObject();
            remote.name = object.value(QStringLiteral("name")).toString();
            remote.type = object.value(QStringLiteral("type")).toString();
        }
        if (remote.name.endsWith(QLatin1Char(':'))) {
            remote.name.chop(1);
        }
        if (!remote.name.isEmpty()) {
            remotes.append(std::move(remote));
        }
    }
    std::sort(remotes.begin(), remotes.end(), [](const RcloneRemote &left, const RcloneRemote &right) {
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });
    return remotes;
}

QStringList RcloneClient::parseRemoteList(const QByteArray &json, QString *error)
{
    const QList<RcloneRemote> parsedRemotes = parseRemoteListWithTypes(json, error);
    QStringList remotes;
    remotes.reserve(parsedRemotes.size());
    for (const RcloneRemote &remote : parsedRemotes) {
        remotes.append(remote.name);
    }
    return remotes;
}

std::optional<bool> RcloneClient::parseDuplicateNameSupport(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject features = document.object().value(QStringLiteral("Features")).toObject();
    const QJsonValue duplicateFiles = features.value(QStringLiteral("DuplicateFiles"));
    const QJsonValue mergeDirectories = features.value(QStringLiteral("MergeDirs"));
    if ((!duplicateFiles.isBool() && !mergeDirectories.isBool()) || features.isEmpty()) {
        setError(error, QStringLiteral("rclone did not report duplicate-name support for this remote"));
        return std::nullopt;
    }

    // MergeDirs is a second signal for duplicate directories.  Treat either
    // capability as requiring the conservative path, including wrapped
    // remotes such as crypt over Google Drive.
    return duplicateFiles.toBool() || mergeDirectories.toBool();
}

std::optional<RcloneRemoteInfo> RcloneClient::parseRemoteInfo(const QByteArray &config, QString *error)
{
    RcloneRemoteInfo info;
    bool foundSection = false;

    const QList<QByteArray> lines = config.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            info.name = line.mid(1, line.size() - 2).trimmed();
            foundSection = !info.name.isEmpty();
            continue;
        }
        if (!foundSection) {
            continue;
        }

        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator < 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();
        if (key == QLatin1String("type")) {
            info.type = value;
        } else if (key == QLatin1String("client_id")) {
            info.hasClientId = !value.isEmpty();
        } else if (key == QLatin1String("root_folder_id")) {
            info.hasRootFolderId = !value.isEmpty();
        }
    }

    if (!foundSection) {
        setError(error, QStringLiteral("No remote section found in rclone configuration"));
        return std::nullopt;
    }
    return info;
}

QList<RcloneItem> RcloneClient::list(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    QList<RcloneItem> items;
    if (!listStreaming(remoteSpec,
                       [&items](const RcloneItem &item) {
                           items.append(item);
                           return true;
                       },
                       error,
                       isCancelled)) {
        return {};
    }
    return items;
}

std::optional<RcloneItem> RcloneClient::stat(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("lsjson"), remoteSpec, QStringLiteral("--stat")}, 60000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return std::nullopt;
    }
    return parseItem(result.standardOutput, error);
}

std::optional<RcloneSpace> RcloneClient::about(const QString &remoteSpec, QString *error, const CancellationCallback &isCancelled) const
{
    const RcloneResult result = run({QStringLiteral("about"), remoteSpec, QStringLiteral("--json")}, 60000, isCancelled);
    if (!result.success()) {
        setError(error, result.errorMessage());
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    RcloneSpace space;
    if (object.contains(QStringLiteral("total"))) {
        space.total = object.value(QStringLiteral("total")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("free"))) {
        space.free = object.value(QStringLiteral("free")).toVariant().toLongLong();
    }
    if (object.contains(QStringLiteral("used"))) {
        space.used = object.value(QStringLiteral("used")).toVariant().toLongLong();
    }
    return space;
}

QString RcloneClient::locateExecutable()
{
    const QString overridden = qEnvironmentVariable("KIO_RCLONE_EXECUTABLE");
    if (!overridden.isEmpty() && QFileInfo(overridden).isExecutable()) {
        return overridden;
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("rclone"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QString userLocal = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.local/bin/rclone");
    return QFileInfo(userLocal).isExecutable() ? userLocal : QString();
}

QList<RcloneItem> RcloneClient::parseItemList(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(error, parseError.errorString());
        return {};
    }

    QList<RcloneItem> items;
    const QJsonArray array = document.array();
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (const auto item = itemFromObject(value.toObject())) {
            items.append(*item);
        }
    }
    return items;
}

std::optional<RcloneItem> RcloneClient::parseItem(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }
    return itemFromObject(document.object());
}

bool RcloneClient::isNotFoundError(const QString &error)
{
    const QString lowered = error.toLower();
    return lowered.contains(QStringLiteral("not found")) || lowered.contains(QStringLiteral("doesn't exist"))
        || lowered.contains(QStringLiteral("does not exist")) || lowered.contains(QStringLiteral("directory not found"))
        || lowered.contains(QStringLiteral("object not found"));
}
