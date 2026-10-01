#include "Location.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace ariadne::location {
namespace {

constexpr auto TrashScheme = "trash"_L1;

QUrl makeTrashUrl(const QString& relative)
{
    QUrl url;
    url.setScheme(TrashScheme);
    url.setHost(u""_s); // an empty authority makes it trash:///path, like elsewhere
    url.setPath(QDir::cleanPath(u"/"_s + relative));
    return url;
}

// Path inside the trash without the leading slash; empty for the trash itself.
QString trashRelative(const QUrl& url)
{
    QString path = url.path();
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    return path;
}

QString homePath()
{
    return QDir::cleanPath(QDir::homePath());
}

QString joinPath(const QString& directory, const QString& name)
{
    return directory.endsWith(u'/') ? directory + name : directory + u'/' + name;
}

} // namespace

QUrl home()
{
    return fromLocalPath(homePath());
}

QUrl trash()
{
    return makeTrashUrl({});
}

QUrl root()
{
    return fromLocalPath(u"/"_s);
}

QUrl fromLocalPath(const QString& path)
{
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

bool isTrash(const QUrl& url)
{
    return url.scheme() == TrashScheme;
}

bool isLocal(const QUrl& url)
{
    return url.isLocalFile();
}

QString trashFilesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/Trash/files"_s;
}

QString localPath(const QUrl& url)
{
    if (isTrash(url)) {
        const QString relative = trashRelative(url);
        return relative.isEmpty() ? trashFilesPath() : joinPath(trashFilesPath(), relative);
    }
    return QDir::cleanPath(url.toLocalFile());
}

QUrl child(const QUrl& directory, const QString& name)
{
    if (isTrash(directory)) {
        const QString relative = trashRelative(directory);
        return makeTrashUrl(relative.isEmpty() ? name : relative + u'/' + name);
    }
    return fromLocalPath(joinPath(localPath(directory), name));
}

std::optional<QUrl> parent(const QUrl& url)
{
    if (isTrash(url)) {
        const QString relative = trashRelative(url);
        if (relative.isEmpty())
            return std::nullopt;
        const qsizetype slash = relative.lastIndexOf(u'/');
        return makeTrashUrl(slash < 0 ? QString() : relative.left(slash));
    }

    const QString path = localPath(url);
    if (path == u"/")
        return std::nullopt;
    const qsizetype slash = path.lastIndexOf(u'/');
    return fromLocalPath(slash <= 0 ? u"/"_s : path.left(slash));
}

bool isAncestorOf(const QUrl& ancestor, const QUrl& url)
{
    if (ancestor.scheme() != url.scheme())
        return false;
    const QString ancestorPath = isTrash(ancestor) ? ancestor.path() : localPath(ancestor);
    const QString path = isTrash(url) ? url.path() : localPath(url);
    if (ancestorPath == path)
        return false;
    return path.startsWith(ancestorPath.endsWith(u'/') ? ancestorPath : ancestorPath + u'/');
}

QString displayName(const QUrl& url)
{
    if (isTrash(url)) {
        const QString relative = trashRelative(url);
        return relative.isEmpty() ? u"Trash"_s : relative.section(u'/', -1);
    }
    const QString path = localPath(url);
    if (path == homePath())
        return u"Home"_s;
    if (path == u"/")
        return u"File System"_s;
    return QFileInfo(path).fileName();
}

QString editableText(const QUrl& url)
{
    if (isTrash(url))
        return u"trash:///"_s + trashRelative(url);

    const QString path = localPath(url);
    const QString home = homePath();
    if (path == home)
        return u"~"_s;
    if (path.startsWith(home + u'/'))
        return u"~"_s + path.mid(home.size());
    return path;
}

std::optional<QUrl> fromUserInput(const QString& text, const QUrl& current)
{
    const QString input = text.trimmed();
    if (input.isEmpty())
        return std::nullopt;

    if (input.startsWith(u"trash:", Qt::CaseInsensitive))
        return makeTrashUrl(input.mid(6));
    if (input.startsWith(u"file:", Qt::CaseInsensitive)) {
        const QUrl url(input);
        if (!url.isLocalFile())
            return std::nullopt;
        return fromLocalPath(url.toLocalFile());
    }
    if (input == u"~")
        return home();
    if (input.startsWith(u"~/"))
        return fromLocalPath(homePath() + input.mid(1));
    if (QDir::isAbsolutePath(input))
        return fromLocalPath(input);

    // A relative path, or some URL scheme we cannot handle yet.
    if (input.contains(u"://"))
        return std::nullopt;
    if (isTrash(current))
        return makeTrashUrl(trashRelative(current) + u'/' + input);
    return fromLocalPath(joinPath(localPath(current), input));
}

std::vector<Crumb> crumbs(const QUrl& url)
{
    std::vector<Crumb> result;

    if (isTrash(url)) {
        result.push_back({u"Trash"_s, u"user-trash"_s, trash()});
        QString accumulated;
        for (const QString& part : trashRelative(url).split(u'/', Qt::SkipEmptyParts)) {
            accumulated = accumulated.isEmpty() ? part : accumulated + u'/' + part;
            result.push_back({part, {}, makeTrashUrl(accumulated)});
        }
        return result;
    }

    const QString path = localPath(url);
    const QString home = homePath();
    QString base;
    if (home != u"/" && (path == home || path.startsWith(home + u'/'))) {
        result.push_back({u"Home"_s, u"user-home"_s, fromLocalPath(home)});
        base = home;
    } else {
        result.push_back({{}, u"drive-harddisk"_s, root()});
        base = u"/"_s;
    }

    QString accumulated = base;
    for (const QString& part : path.mid(base.size()).split(u'/', Qt::SkipEmptyParts)) {
        accumulated = joinPath(accumulated, part);
        result.push_back({part, {}, fromLocalPath(accumulated)});
    }
    return result;
}

QString directoryIconName(const QString& path)
{
    static const QHash<QString, QString> icons = [] {
        QHash<QString, QString> map;
        const std::pair<QStandardPaths::StandardLocation, QString> known[] = {
            {QStandardPaths::DesktopLocation, u"user-desktop"_s},
            {QStandardPaths::DocumentsLocation, u"folder-documents"_s},
            {QStandardPaths::DownloadLocation, u"folder-download"_s},
            {QStandardPaths::MusicLocation, u"folder-music"_s},
            {QStandardPaths::PicturesLocation, u"folder-pictures"_s},
            {QStandardPaths::MoviesLocation, u"folder-videos"_s},
            {QStandardPaths::TemplatesLocation, u"folder-templates"_s},
            {QStandardPaths::PublicShareLocation, u"folder-publicshare"_s},
        };
        const QString home = homePath();
        for (const auto& [location, icon] : known) {
            const QString dir = QDir::cleanPath(QStandardPaths::writableLocation(location));
            if (!dir.isEmpty() && dir != home)
                map.insert(dir, icon);
        }
        map.insert(home, u"user-home"_s);
        return map;
    }();
    return icons.value(QDir::cleanPath(path));
}

} // namespace ariadne::location
