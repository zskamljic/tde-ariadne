#include "Location.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace ariadne::location {
namespace {

constexpr auto TrashScheme = "trash"_L1;
constexpr auto ArchiveScheme = "archive"_L1;

QUrl makeArchiveUrl(const QString& path)
{
    QUrl url;
    url.setScheme(ArchiveScheme);
    url.setHost(u""_s);
    url.setPath(QDir::cleanPath(path));
    return url;
}

// The archive file a path goes through: the first part of it that is a file on disk.
std::optional<std::pair<QString, QString>> splitAtFile(const QString& path)
{
    for (qsizetype slash = path.indexOf(u'/', 1);; slash = path.indexOf(u'/', slash + 1)) {
        const QString prefix = slash < 0 ? path : path.left(slash);
        if (QFileInfo(prefix).isFile())
            return std::pair {prefix, slash < 0 ? QString() : path.mid(slash + 1)};
        if (slash < 0)
            return std::nullopt;
    }
}

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

bool isArchive(const QUrl& url)
{
    return url.scheme() == ArchiveScheme;
}

QUrl archiveRoot(const QString& archivePath)
{
    return makeArchiveUrl(archivePath);
}

std::optional<ArchivePlace> archivePlace(const QUrl& url)
{
    if (!isArchive(url))
        return std::nullopt;
    const auto split = splitAtFile(url.path());
    if (!split)
        return std::nullopt;
    return ArchivePlace {split->first, split->second};
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
    if (isArchive(url))
        return url.path();
    return QDir::cleanPath(url.toLocalFile());
}

QUrl child(const QUrl& directory, const QString& name)
{
    if (isTrash(directory)) {
        const QString relative = trashRelative(directory);
        return makeTrashUrl(relative.isEmpty() ? name : relative + u'/' + name);
    }
    if (isArchive(directory))
        return makeArchiveUrl(joinPath(directory.path(), name));
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
    // From the top of an archive, up is the folder the archive is in.
    if (isArchive(url)) {
        const auto place = archivePlace(url);
        const QString path = url.path();
        if (!place || place->inside.isEmpty())
            return fromLocalPath(QFileInfo(place ? place->file : path).absolutePath());
        return makeArchiveUrl(path.left(path.lastIndexOf(u'/')));
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

    // The top of an archive keeps a slash, which makes it a folder when typed in again.
    const auto place = archivePlace(url);
    const QString path = localPath(url) + (place && place->inside.isEmpty() ? u"/"_s : QString());
    const QString home = homePath();
    if (path == home)
        return u"~"_s;
    if (path.startsWith(home + u'/'))
        return u"~"_s + path.mid(home.size());
    return path;
}

namespace {

// A path typed in: a folder, a file, or somewhere inside an archive file ("~/a.zip/" for the
// top of it, as a trailing slash makes a file a folder).
QUrl fromTypedPath(const QString& path)
{
    const QFileInfo info(path);
    const bool asFolder = path.endsWith(u'/') && path.size() > 1;
    if (info.exists() && !(asFolder && info.isFile()))
        return fromLocalPath(path);
    if (splitAtFile(QDir::cleanPath(path)))
        return makeArchiveUrl(path);
    return fromLocalPath(path);
}

} // namespace

std::optional<QUrl> fromUserInput(const QString& text, const QUrl& current)
{
    const QString input = text.trimmed();
    if (input.isEmpty())
        return std::nullopt;
    if (input.startsWith(u"archive:", Qt::CaseInsensitive))
        return makeArchiveUrl(QUrl(input).path());

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
        return fromTypedPath(homePath() + input.mid(1));
    if (QDir::isAbsolutePath(input))
        return fromTypedPath(input);

    // A relative path, or some URL scheme we cannot handle yet.
    if (input.contains(u"://"))
        return std::nullopt;
    if (isTrash(current))
        return makeTrashUrl(trashRelative(current) + u'/' + input);
    if (isArchive(current))
        return makeArchiveUrl(joinPath(current.path(), input));
    return fromTypedPath(joinPath(localPath(current), input));
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

    // Inside an archive: the way to the archive file, the archive, then the folders in it.
    if (const auto place = archivePlace(url)) {
        result = crumbs(fromLocalPath(QFileInfo(place->file).absolutePath()));
        const QUrl top = archiveRoot(place->file);
        result.push_back({QFileInfo(place->file).fileName(), u"package-x-generic"_s, top});
        QUrl accumulated = top;
        for (const QString& part : place->inside.split(u'/', Qt::SkipEmptyParts)) {
            accumulated = child(accumulated, part);
            result.push_back({part, {}, accumulated});
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
