#include "Bookmarks.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

QUrl normalized(const QUrl& url)
{
    if (url.isLocalFile())
        return QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile()));
    return url.adjusted(QUrl::StripTrailingSlash);
}

QList<Bookmark> defaultBookmarks()
{
    QList<Bookmark> items;
    for (const auto location : {QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation}) {
        const QString path = QStandardPaths::writableLocation(location);
        if (!path.isEmpty() && QDir::cleanPath(path) != QDir::cleanPath(QDir::homePath()) && QFileInfo(path).isDir())
            items << Bookmark {QUrl::fromLocalFile(QDir::cleanPath(path)), {}};
    }
    return items;
}

} // namespace

Bookmarks::Bookmarks(QString filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(std::move(filePath))
{
    load();

    const QString directory = QFileInfo(m_filePath).absolutePath();
    if (QFileInfo(directory).isDir())
        m_watcher.addPath(directory);
    watchFile();

    const auto reload = [this] {
        watchFile();
        const QList<Bookmark> previous = m_items;
        load();
        if (m_items != previous)
            emit changed();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, reload);
}

QString Bookmarks::defaultFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/gtk-3.0/bookmarks"_s;
}

QString Bookmarks::displayName(const Bookmark& bookmark)
{
    if (!bookmark.label.isEmpty())
        return bookmark.label;
    if (bookmark.url.isLocalFile()) {
        const QString name = QFileInfo(bookmark.url.toLocalFile()).fileName();
        return name.isEmpty() ? bookmark.url.toLocalFile() : name;
    }
    return bookmark.url.toDisplayString();
}

qsizetype Bookmarks::indexOf(const QUrl& url) const
{
    const QUrl wanted = normalized(url);
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items[i].url == wanted)
            return i;
    }
    return -1;
}

void Bookmarks::add(const QUrl& url, qsizetype index)
{
    if (contains(url))
        return;
    if (index < 0 || index > m_items.size())
        index = m_items.size();
    m_items.insert(index, Bookmark {normalized(url), {}});
    save();
}

void Bookmarks::remove(const QUrl& url)
{
    const qsizetype index = indexOf(url);
    if (index < 0)
        return;
    m_items.removeAt(index);
    save();
}

void Bookmarks::rename(const QUrl& url, const QString& label)
{
    const qsizetype index = indexOf(url);
    if (index < 0)
        return;
    m_items[index].label = label.trimmed();
    save();
}

void Bookmarks::move(qsizetype from, qsizetype to)
{
    if (from < 0 || from >= m_items.size())
        return;
    to = std::clamp<qsizetype>(to, 0, m_items.size());
    if (to > from)
        --to;
    if (to == from)
        return;
    m_items.move(from, to);
    save();
}

void Bookmarks::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_items = file.exists() ? QList<Bookmark> {} : defaultBookmarks();
        return;
    }

    QList<Bookmark> items;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype space = line.indexOf(u' ');
        Bookmark bookmark;
        bookmark.url = normalized(QUrl::fromEncoded(line.left(space).toUtf8()));
        if (space > 0)
            bookmark.label = line.mid(space + 1).trimmed();
        if (bookmark.url.isValid())
            items << bookmark;
    }
    m_items = items;
}

void Bookmarks::save()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (const Bookmark& bookmark : std::as_const(m_items)) {
            QByteArray line = bookmark.url.toEncoded();
            if (!bookmark.label.isEmpty())
                line += ' ' + bookmark.label.toUtf8();
            file.write(line + '\n');
        }
        file.commit();
    }
    watchFile();
    emit changed();
}

void Bookmarks::watchFile()
{
    // QSaveFile replaces the file, which drops the inotify watch; add it again.
    if (QFile::exists(m_filePath) && !m_watcher.files().contains(m_filePath))
        m_watcher.addPath(m_filePath);
}

} // namespace ariadne
