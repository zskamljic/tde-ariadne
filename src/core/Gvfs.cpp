#include "Gvfs.hpp"

#include "Location.hpp"

#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne::gvfs {
namespace {

QString gio()
{
    return QStandardPaths::findExecutable(u"gio"_s);
}

struct Output {
    bool ok;
    QString text;
    QString error;
};

Output runGio(const QStringList& arguments, int timeoutMs = 15'000)
{
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(gio(), arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {false, {}, u"gio did not answer in time."_s};
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
        QString::fromLocal8Bit(process.readAllStandardOutput()),
        QString::fromLocal8Bit(process.readAllStandardError()).trimmed()};
}

// "gio: smb://host/share/: Connection refused" → "Connection refused".
QString cleaned(const QString& error, const QString& uri)
{
    QString text = error.section(u'\n', 0, 0);
    if (text.startsWith(u"gio: "))
        text = text.mid(5);
    const QString withSlash = uri.endsWith(u'/') ? uri : uri + u'/';
    for (const QString& prefix : {withSlash + u": "_s, uri + u": "_s}) {
        if (text.startsWith(prefix))
            return text.mid(prefix.size());
    }
    return text.isEmpty() ? u"It could not be reached."_s : text;
}

QString normalized(QString uri)
{
    if (!uri.endsWith(u'/'))
        uri += u'/';
    return uri;
}

QString localPathOf(const QString& uri)
{
    const Output info = runGio({u"info"_s, u"-a"_s, u"standard::name"_s, uri}, 10'000);
    for (const QString& line : info.text.split(u'\n')) {
        if (line.startsWith(u"local path: "))
            return line.mid(12).trimmed();
    }
    return {};
}

} // namespace

bool isAvailable()
{
    return !gio().isEmpty();
}

QList<Location> parseMountList(const QString& output)
{
    struct Record {
        QString kind; // Drive, Volume or Mount
        QString name;
        QString uri;
        QString type;
        QString icon;
    };
    static const QRegularExpression header(u"^\\s*(Drive|Volume|Mount)\\(\\d+\\): (.*)$"_s);
    static const QRegularExpression icon(u"\\[([^\\]]+)\\]"_s);

    QList<Record> records;
    for (const QString& line : output.split(u'\n')) {
        if (const auto match = header.match(line); match.hasMatch()) {
            Record record {match.captured(1), match.captured(2), {}, {}, {}};
            if (record.kind == u"Mount") {
                const qsizetype arrow = record.name.lastIndexOf(u" -> ");
                if (arrow >= 0) {
                    record.uri = record.name.mid(arrow + 4).trimmed();
                    record.name = record.name.left(arrow);
                }
            }
            records << record;
            continue;
        }
        if (records.isEmpty())
            continue;
        Record& record = records.last();
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(u"Type: "))
            record.type = trimmed.mid(6);
        else if (trimmed.startsWith(u"activation_root="))
            record.uri = trimmed.mid(16);
        else if (trimmed.startsWith(u"themed icons:") && record.icon.isEmpty())
            record.icon = icon.match(trimmed).captured(1);
    }

    // Drives and their volumes come from UDisks2 and are listed from there instead.
    const auto fromUDisks = [](const Record& record) { return record.type.contains(u"UDisks2"); };
    // A mounted volume is listed twice, as the volume and as its mount (and gvfs may add a
    // second mount of its own). One entry per URI, named after the volume, which names it best.
    QList<Location> locations;
    const auto findUri = [&](const QString& uri) { return std::ranges::find(locations, uri, &Location::uri); };
    for (const Record& record : std::as_const(records)) {
        if (record.kind != u"Volume" || fromUDisks(record) || record.uri.isEmpty())
            continue;
        if (findUri(normalized(record.uri)) == locations.end())
            locations << Location {record.name, normalized(record.uri), record.icon, false, {}};
    }
    for (const Record& record : std::as_const(records)) {
        if (record.kind != u"Mount" || fromUDisks(record) || record.uri.isEmpty() || record.uri.startsWith(u"file:"))
            continue;
        if (const auto it = findUri(normalized(record.uri)); it != locations.end())
            it->mounted = true;
        else
            locations << Location {record.name, normalized(record.uri), record.icon, true, {}};
    }
    // Mounted ones first, as before.
    std::ranges::stable_partition(locations, &Location::mounted);
    for (Location& location : locations) {
        if (location.iconName.isEmpty())
            location.iconName = u"folder-remote"_s;
    }
    return locations;
}

QList<Location> scan()
{
    if (!isAvailable())
        return {};
    const Output list = runGio({u"mount"_s, u"-li"_s});
    QList<Location> locations = parseMountList(list.text);
    for (Location& location : locations) {
        if (location.mounted)
            location.localPath = localPathOf(location.uri);
    }
    return locations;
}

std::expected<void, QString> unmount(const QString& uri)
{
    const Output result = runGio({u"mount"_s, u"-u"_s, uri}, 60'000);
    if (!result.ok)
        return std::unexpected(cleaned(result.error.isEmpty() ? result.text : result.error, uri));
    return {};
}

std::optional<Prompt> parsePrompt(const QString& pending)
{
    // gio ends each question with "Name [default]: " and waits for a line.
    static const QRegularExpression ask(u"(User|Domain|Password|Choice)(?: \\[([^\\]]*)\\])?: $"_s);
    const auto match = ask.match(pending);
    if (!match.hasMatch())
        return std::nullopt;

    const QString word = match.captured(1);
    Prompt prompt {Prompt::Kind::User, match.captured(2), {}, {}};
    if (word == u"Domain") {
        prompt.kind = Prompt::Kind::Domain;
    } else if (word == u"Password") {
        prompt.kind = Prompt::Kind::Password;
    } else if (word == u"Choice") {
        // Before it: the question, then "[0] first choice", "[1] second choice", …
        prompt.kind = Prompt::Kind::Choice;
        static const QRegularExpression choice(u"^\\[(\\d+)\\] (.*)$"_s);
        QStringList question;
        for (const QString& line : pending.left(match.capturedStart()).split(u'\n', Qt::SkipEmptyParts)) {
            if (const auto option = choice.match(line.trimmed()); option.hasMatch())
                prompt.choices << option.captured(2);
            else if (prompt.choices.isEmpty())
                question << line.trimmed();
        }
        prompt.question = question.join(u'\n');
    }
    return prompt;
}

MountOperation::MountOperation(QString uri, Answer answer, QObject* parent)
    : QObject(parent)
    , m_uri(std::move(uri))
    , m_answer(std::move(answer))
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &MountOperation::readOutput);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_output += m_process.readAll();
        if (m_cancelled) {
            emit finished(std::unexpected(QString()));
            return;
        }
        const QString output = QString::fromLocal8Bit(m_output);
        // Mounting something already mounted is fine too.
        if ((status == QProcess::NormalExit && code == 0) || output.contains(u"already mounted"_s)) {
            const QString path = localPathOf(m_uri);
            if (!path.isEmpty())
                emit finished(path);
            else
                emit finished(std::unexpected(u"It was mounted, but has no folder to show it in."_s));
            return;
        }
        emit finished(std::unexpected(cleaned(output.trimmed().section(u'\n', -1), m_uri)));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit finished(std::unexpected(u"The gio tool could not be started."_s));
    });
}

void MountOperation::start()
{
    if (!isAvailable()) {
        emit finished(std::unexpected(u"Mounting network locations needs gvfs and its gio tool."_s));
        return;
    }
    m_process.start(gio(), {u"mount"_s, m_uri});
}

void MountOperation::readOutput()
{
    m_output += m_process.readAll();
    const auto prompt = parsePrompt(QString::fromLocal8Bit(m_output));
    if (!prompt)
        return;
    // Everything up to the question is dealt with; what comes next is a new question.
    m_output.clear();
    const std::optional<QString> answer = m_answer ? m_answer(*prompt) : std::nullopt;
    if (!answer) {
        m_cancelled = true;
        m_process.kill();
        return;
    }
    m_process.write((*answer + u'\n').toLocal8Bit());
}

bool isGvfsPath(const QString& path)
{
    static const QString folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + u"/gvfs/"_s;
    return path.startsWith(folder) && path.size() > folder.size();
}

std::optional<FileEntry> parseListLine(
    const QString& line, const QUrl& directory, const QString& directoryPath, const QMimeDatabase& mimeDatabase)
{
    // URI, size, (type), then the attributes asked for as key=value, separated by spaces.
    const QStringList fields = line.split(u'\t');
    if (fields.size() < 3)
        return std::nullopt;
    const QString name = QUrl(fields[0]).fileName();
    if (name.isEmpty())
        return std::nullopt;

    QHash<QString, QString> attributes;
    if (fields.size() > 3) {
        for (const QString& pair : fields[3].split(u' ', Qt::SkipEmptyParts))
            attributes.insert(pair.section(u'=', 0, 0), pair.section(u'=', 1));
    }

    FileEntry entry;
    entry.name = name;
    entry.path = directoryPath + u'/' + name;
    entry.url = location::child(directory, name);
    entry.isRemote = true;
    entry.isDir = fields[2] == u"(directory)";
    entry.isSymlink = attributes.value(u"standard::is-symlink"_s) == u"TRUE";
    entry.isHidden = name.startsWith(u'.') || attributes.value(u"standard::is-hidden"_s) == u"TRUE";
    entry.size = entry.isDir ? 0 : fields[1].toLongLong();
    if (const QString modified = attributes.value(u"time::modified"_s); !modified.isEmpty())
        entry.modified = QDateTime::fromSecsSinceEpoch(modified.toLongLong());

    QMimeType mime;
    if (entry.isDir)
        mime = mimeDatabase.mimeTypeForName(u"inode/directory"_s);
    else if (const QString type = attributes.value(u"standard::content-type"_s); !type.isEmpty())
        mime = mimeDatabase.mimeTypeForName(type);
    if (!mime.isValid() || mime.isDefault())
        mime = mimeDatabase.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
    entry.mimeType = mime.name();
    entry.mimeComment = mime.comment();
    entry.iconName = mime.iconName();
    entry.genericIconName = mime.genericIconName();
    return entry;
}

Listing::Listing(QUrl location, QObject* parent)
    : QObject(parent)
    , m_location(std::move(location))
    , m_path(location::localPath(m_location))
{
    // A few times a second is plenty for the views, which sort and lay out every batch.
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(150);
    connect(&m_flushTimer, &QTimer::timeout, this, &Listing::flush);

    m_process.setStandardInputFile(QProcess::nullDevice());
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Listing::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this,
        [this] { m_errors += QString::fromLocal8Bit(m_process.readAllStandardError()); });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        if (!m_pending.isEmpty()) {
            m_pending += '\n';
            readOutput();
        }
        flush();
        if (status == QProcess::NormalExit && code == 0) {
            emit finished({});
            return;
        }
        // "gio: file:///run/user/1000/gvfs/…: No such file or directory" → the message alone.
        static const QRegularExpression prefix(u"^gio: \\S+: "_s);
        QString error = m_errors.trimmed().section(u'\n', 0, 0).remove(prefix);
        emit finished(std::unexpected(error.isEmpty() ? u"The folder could not be listed."_s : error));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit finished(std::unexpected(u"The gio tool could not be started."_s));
    });
}

Listing::~Listing()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void Listing::start()
{
    m_process.start(gio(),
        {u"list"_s, u"-l"_s, u"-h"_s, u"-u"_s, u"-a"_s,
            u"standard::content-type,standard::is-hidden,standard::is-symlink,time::modified"_s, m_path});
}

void Listing::readOutput()
{
    m_pending += m_process.readAllStandardOutput();
    const qsizetype end = m_pending.lastIndexOf('\n');
    if (end < 0)
        return;
    const QByteArray complete = m_pending.left(end);
    m_pending.remove(0, end + 1);
    for (const QByteArray& line : complete.split('\n')) {
        if (auto entry = parseListLine(QString::fromUtf8(line), m_location, m_path, m_mimeDatabase))
            m_entries.push_back(std::move(*entry));
    }
    // The first entries right away, so the folder does not look empty; then in batches.
    if (m_reported == 0 && !m_entries.empty())
        flush();
    else if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void Listing::flush()
{
    m_flushTimer.stop();
    if (m_reported == m_entries.size())
        return;
    const std::vector<FileEntry> batch(m_entries.begin() + static_cast<std::ptrdiff_t>(m_reported), m_entries.end());
    m_reported = m_entries.size();
    emit found(batch);
}

} // namespace ariadne::gvfs
