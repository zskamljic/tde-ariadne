#include "Applications.hpp"

#include "Collation.hpp"
#include "Terminal.hpp"

#include <QCollator>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

// Undoes the escapes of desktop entry string values: \s \n \t \r and \\.
QString unescape(const QString& value)
{
    QString result;
    result.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (value[i] != u'\\' || i + 1 == value.size()) {
            result += value[i];
            continue;
        }
        switch (value[++i].unicode()) {
        case 's':
            result += u' ';
            break;
        case 'n':
            result += u'\n';
            break;
        case 't':
            result += u'\t';
            break;
        case 'r':
            result += u'\r';
            break;
        default:
            result += value[i];
        }
    }
    return result;
}

// Splits an Exec value into arguments, following the quoting rules of the spec.
QStringList splitExec(const QString& exec)
{
    QStringList arguments;
    QString current;
    bool quoted = false;
    bool inArgument = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec[i];
        if (quoted) {
            if (c == u'\\' && i + 1 < exec.size() && QStringView(u"\"`$\\").contains(exec[i + 1]))
                current += exec[++i];
            else if (c == u'"')
                quoted = false;
            else
                current += c;
        } else if (c.isSpace()) {
            if (inArgument)
                arguments << std::exchange(current, {});
            inArgument = false;
        } else {
            inArgument = true;
            if (c == u'"')
                quoted = true;
            else
                current += c;
        }
    }
    if (inArgument)
        arguments << current;
    return arguments;
}

std::optional<DesktopApp> parseDesktopFile(const QString& path, const QString& id, bool& hidden)
{
    hidden = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    const QString locale = QLocale().name();
    const QString language = locale.section(u'_', 0, 0);
    QHash<QString, QString> values;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == u"[Desktop Entry]";
            continue;
        }
        const qsizetype equals = line.indexOf(u'=');
        if (inEntry && equals > 0 && !line.startsWith(u'#'))
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }

    if (values.value(u"Hidden"_s) == u"true") {
        hidden = true;
        return std::nullopt;
    }
    if (values.value(u"Type"_s) != u"Application" || values.value(u"Exec"_s).isEmpty())
        return std::nullopt;
    if (const QString tryExec = unescape(values.value(u"TryExec"_s));
        !tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty() && !QFileInfo(tryExec).isExecutable())
        return std::nullopt;

    DesktopApp app;
    app.id = id;
    app.filePath = path;
    app.name = values.value(
        u"Name[%1]"_s.arg(locale), values.value(u"Name[%1]"_s.arg(language), values.value(u"Name"_s, id)));
    app.name = unescape(app.name);
    app.exec = unescape(values.value(u"Exec"_s));
    app.iconName = unescape(values.value(u"Icon"_s));
    app.workingDirectory = unescape(values.value(u"Path"_s));
    app.mimeTypes = values.value(u"MimeType"_s).split(u';', Qt::SkipEmptyParts);
    app.terminal = values.value(u"Terminal"_s) == u"true";
    app.noDisplay = values.value(u"NoDisplay"_s) == u"true";
    return app;
}

QStringList mimeAppsFiles()
{
    QStringList desktops;
    for (const QString& desktop : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts))
        desktops << desktop.toLower();

    QStringList files;
    const auto addDirectory = [&](const QString& directory) {
        for (const QString& desktop : std::as_const(desktops))
            files << u"%1/%2-mimeapps.list"_s.arg(directory, desktop);
        files << directory + u"/mimeapps.list"_s;
    };
    for (const QString& directory : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation))
        addDirectory(directory);
    for (const QString& directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation))
        addDirectory(directory);
    return files;
}

QString userMimeAppsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/mimeapps.list"_s;
}

// The mime types to look at for `mimeType`, most specific first.
QStringList typeChain(const QString& mimeType)
{
    const QMimeType mime = QMimeDatabase().mimeTypeForName(mimeType);
    QStringList types {mimeType};
    if (mime.isValid()) {
        types << mime.aliases();
        types << mime.allAncestors();
    }
    types.removeDuplicates();
    return types;
}

// Sets `key` in `group` of an ini-style file held as lines, keeping everything else.
void setKey(QStringList& lines, const QString& group, const QString& key, const QString& value)
{
    const QString header = u'[' + group + u']';
    const QString entry = key + u'=' + value;
    qsizetype start = lines.indexOf(header);
    if (start < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines << QString();
        lines << header << entry;
        return;
    }
    qsizetype insertAt = start + 1;
    for (qsizetype i = start + 1; i < lines.size() && !lines[i].startsWith(u'['); ++i) {
        if (lines[i].startsWith(key + u'=')) {
            lines[i] = entry;
            return;
        }
        if (!lines[i].trimmed().isEmpty())
            insertAt = i + 1;
    }
    lines.insert(insertAt, entry);
}

QString keyValue(const QStringList& lines, const QString& group, const QString& key)
{
    const qsizetype start = lines.indexOf(u'[' + group + u']');
    if (start < 0)
        return {};
    for (qsizetype i = start + 1; i < lines.size() && !lines[i].startsWith(u'['); ++i) {
        if (lines[i].startsWith(key + u'='))
            return lines[i].mid(key.size() + 1);
    }
    return {};
}

QString fileUrl(const QString& path)
{
    return QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
}

} // namespace

Applications Applications::load()
{
    Applications applications;
    QSet<QString> seen;
    // Earlier folders (the user's own first) override applications with the same id.
    for (const QString& directory : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        const QDir base(directory);
        for (const auto& entry : QDirListing(directory, {u"*.desktop"_s},
                 QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FilesOnly)) {
            const QString id = base.relativeFilePath(entry.absoluteFilePath()).replace(u'/', u'-');
            if (seen.contains(id))
                continue;
            seen.insert(id);
            bool hidden = false;
            if (auto app = parseDesktopFile(entry.absoluteFilePath(), id, hidden)) {
                applications.m_byId.insert(id, applications.m_apps.size());
                applications.m_apps << std::move(*app);
            }
        }
    }
    applications.readMimeAppsLists();
    return applications;
}

void Applications::readMimeAppsLists()
{
    for (const QString& path : mimeAppsFiles()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QHash<QString, QStringList>* group = nullptr;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(u'[')) {
                group = line == u"[Default Applications]" ? &m_defaults
                    : line == u"[Added Associations]"     ? &m_added
                    : line == u"[Removed Associations]"   ? &m_removed
                                                          : nullptr;
                continue;
            }
            const qsizetype equals = line.indexOf(u'=');
            if (group && equals > 0)
                (*group)[line.left(equals).trimmed()] << line.mid(equals + 1).split(u';', Qt::SkipEmptyParts);
        }
    }
}

QList<const DesktopApp*> Applications::visible() const
{
    QList<const DesktopApp*> apps;
    for (const DesktopApp& app : m_apps) {
        if (!app.noDisplay)
            apps << &app;
    }
    const QCollator collator = naturalCollator();
    std::ranges::sort(
        apps, [&](const DesktopApp* a, const DesktopApp* b) { return collator.compare(a->name, b->name) < 0; });
    return apps;
}

const DesktopApp* Applications::find(const QString& id) const
{
    const auto it = m_byId.constFind(id);
    return it == m_byId.cend() ? nullptr : &m_apps[*it];
}

const DesktopApp* Applications::defaultFor(const QString& mimeType) const
{
    for (const QString& type : typeChain(mimeType)) {
        for (const QString& id : m_defaults.value(type)) {
            if (const DesktopApp* app = find(id))
                return app;
        }
    }
    return nullptr;
}

QList<const DesktopApp*> Applications::forMimeType(const QString& mimeType) const
{
    const QStringList types = typeChain(mimeType);
    QStringList removed;
    for (const QString& type : types)
        removed << m_removed.value(type);

    QList<const DesktopApp*> result;
    const auto add = [&](const DesktopApp* app) {
        if (app && !result.contains(app) && !removed.contains(app->id))
            result << app;
    };

    add(defaultFor(mimeType));
    const QCollator collator = naturalCollator();
    for (const QString& type : types) {
        for (const QString& id : m_added.value(type))
            add(find(id));
        QList<const DesktopApp*> matching;
        for (const DesktopApp& app : m_apps) {
            if (app.mimeTypes.contains(type))
                matching << &app;
        }
        std::ranges::sort(
            matching, [&](const DesktopApp* a, const DesktopApp* b) { return collator.compare(a->name, b->name) < 0; });
        for (const DesktopApp* app : std::as_const(matching))
            add(app);
    }
    return result;
}

std::expected<void, QString> Applications::setDefault(const QString& mimeType, const QString& appId)
{
    const QString path = userMimeAppsFile();
    QStringList lines;
    if (QFile file(path); file.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(file.readAll()).split(u'\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();

    setKey(lines, u"Default Applications"_s, mimeType, appId + u';');
    QStringList added = keyValue(lines, u"Added Associations"_s, mimeType).split(u';', Qt::SkipEmptyParts);
    added.removeAll(appId);
    added.prepend(appId);
    setKey(lines, u"Added Associations"_s, mimeType, added.join(u';') + u';');

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return std::unexpected(file.errorString());
    file.write((lines.join(u'\n') + u'\n').toUtf8());
    if (!file.commit())
        return std::unexpected(file.errorString());
    return {};
}

QList<QStringList> Applications::commandLines(const DesktopApp& app, const QStringList& paths)
{
    const QStringList arguments = splitExec(app.exec);
    if (arguments.isEmpty())
        return {};

    const bool takesList = arguments.contains(u"%F"_s) || arguments.contains(u"%U"_s);
    const bool takesOne = std::ranges::any_of(
        arguments, [](const QString& argument) { return argument.contains(u"%f") || argument.contains(u"%u"); });

    const auto expand = [&](const QStringList& files) {
        QStringList command;
        for (const QString& argument : arguments) {
            if (argument == u"%F") {
                command << files;
            } else if (argument == u"%U") {
                for (const QString& file : files)
                    command << fileUrl(file);
            } else if (argument == u"%i") {
                if (!app.iconName.isEmpty())
                    command << u"--icon"_s << app.iconName;
            } else {
                QString expanded;
                bool hadCode = false;
                for (qsizetype i = 0; i < argument.size(); ++i) {
                    if (argument[i] != u'%' || i + 1 == argument.size()) {
                        expanded += argument[i];
                        continue;
                    }
                    hadCode = true;
                    switch (argument[++i].unicode()) {
                    case 'f':
                        expanded += files.value(0);
                        break;
                    case 'u':
                        expanded += files.isEmpty() ? QString() : fileUrl(files.first());
                        break;
                    case 'c':
                        expanded += app.name;
                        break;
                    case 'k':
                        expanded += app.filePath;
                        break;
                    case '%':
                        expanded += u'%';
                        break;
                    default: // deprecated codes expand to nothing
                        break;
                    }
                }
                // An argument that was only a field code with nothing to put in disappears.
                if (!expanded.isEmpty() || !hadCode)
                    command << expanded;
            }
        }
        return command;
    };

    QList<QStringList> commands;
    if (!takesList && takesOne && paths.size() > 1) {
        for (const QString& path : paths)
            commands << expand({path});
    } else {
        commands << expand(paths);
    }
    return commands;
}

std::expected<void, QString> Applications::launch(
    const DesktopApp& app, const QStringList& paths, const QString& preferredTerminal)
{
    QString workingDirectory = app.workingDirectory;
    if (workingDirectory.isEmpty())
        workingDirectory = paths.isEmpty() ? QDir::homePath() : QFileInfo(paths.first()).absolutePath();

    for (QStringList command : commandLines(app, paths)) {
        if (app.terminal) {
            const auto terminal = terminal::find(preferredTerminal);
            if (!terminal)
                return std::unexpected(u"%1 needs a terminal, and none was found."_s.arg(app.name));
            command = terminal->run(command);
        }
        if (command.isEmpty())
            return std::unexpected(u"%1 has no command to run."_s.arg(app.name));
        const QString program = command.takeFirst();
        if (!QProcess::startDetached(program, command, workingDirectory))
            return std::unexpected(u"%1 could not be started."_s.arg(app.name));
    }
    return {};
}

Executable executableKind(const QString& path, const QString& mimeType)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isExecutable())
        return Executable::No;
    const QMimeType mime = QMimeDatabase().mimeTypeForName(mimeType);
    if (!mime.isValid() || !mime.inherits(u"application/x-executable"_s))
        return Executable::No;
    return mime.name().startsWith(u"text/") || mime.inherits(u"text/plain"_s) ? Executable::Script
                                                                              : Executable::Program;
}

std::expected<void, QString> runExecutable(const QString& path, bool inTerminal, const QString& terminal)
{
    const QFileInfo info(path);
    QStringList command {info.absoluteFilePath()};
    if (inTerminal) {
        const auto found = terminal::find(terminal);
        if (!found)
            return std::unexpected(u"No terminal was found to run “%1” in."_s.arg(info.fileName()));
        command = found->run(command);
    }
    const QString program = command.takeFirst();
    if (!QProcess::startDetached(program, command, info.absolutePath()))
        return std::unexpected(u"“%1” could not be started."_s.arg(info.fileName()));
    return {};
}

} // namespace ariadne
