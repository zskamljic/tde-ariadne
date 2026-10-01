#include "DirectoryModel.hpp"

#include "core/Location.hpp"
#include "core/Search.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QMimeData>
#include <QPointer>
#include <QtConcurrentRun>

#include <memory>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

QString formatModified(const QDateTime& time)
{
    if (!time.isValid())
        return {};
    const QLocale locale;
    const QDate date = time.date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return locale.toString(time.time(), QLocale::ShortFormat);
    if (date == today.addDays(-1))
        return u"Yesterday"_s;
    if (date.year() == today.year())
        return locale.toString(date, u"d MMM"_s);
    return locale.toString(date, u"d MMM yyyy"_s);
}

QString formatSize(const FileEntry& entry)
{
    if (entry.isDir) {
        if (entry.childCount < 0)
            return u"—"_s;
        return entry.childCount == 1 ? u"1 item"_s : u"%1 items"_s.arg(entry.childCount);
    }
    return QLocale().formattedDataSize(entry.size, 1, QLocale::DataSizeSIFormat);
}

// Programs get the generic executable icon rather than, say, a wine glass for Windows ones.
const QHash<QString, QString> builtinIconOverrides {
    {u"application/x-msdownload"_s, u"application-x-executable"_s},
    {u"application/x-ms-dos-executable"_s, u"application-x-executable"_s},
    {u"application/vnd.microsoft.portable-executable"_s, u"application-x-executable"_s},
    {u"application/x-msi"_s, u"application-x-executable"_s},
    {u"application/x-pie-executable"_s, u"application-x-executable"_s},
    {u"application/vnd.appimage"_s, u"application-x-executable"_s},
};

} // namespace

DirectoryModel::DirectoryModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_iconOverrides(builtinIconOverrides)
{
    connect(&m_thumbnailer, &Thumbnailer::ready, this, &DirectoryModel::thumbnailReady);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(200);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { startListing(true); });
    m_expandedRefreshTimer.setSingleShot(true);
    m_expandedRefreshTimer.setInterval(200);
    connect(&m_expandedRefreshTimer, &QTimer::timeout, this, &DirectoryModel::refreshExpanded);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        if (path == m_root.path) {
            m_refreshTimer.start();
        } else {
            m_changedFolders.insert(path);
            m_expandedRefreshTimer.start();
        }
    });
}

DirectoryModel::~DirectoryModel()
{
    m_searchStop.request_stop();
}

void DirectoryModel::setLocation(const QUrl& url)
{
    m_location = url;
    stopSearch();
    m_refreshTimer.stop();
    clearThumbnails();
    watch(location::localPath(url));
    startListing(false); // first, so views see isLoading() during the reset

    beginResetModel();
    m_root.entries.clear();
    m_root.expanded.clear();
    m_root.path = location::localPath(url);
    m_root.url = url;
    endResetModel();
}

void DirectoryModel::reload()
{
    if (!m_query.isEmpty())
        setSearch(m_location, m_query, m_searchHidden);
    else
        startListing(true);
}

void DirectoryModel::stopSearch()
{
    stopRemoteListing();
    m_searchStop.request_stop();
    m_searchStop = {};
    m_query.clear();
    m_searching = false;
}

void DirectoryModel::setSearch(const QUrl& root, const QString& query, bool includeHidden)
{
    stopSearch();
    m_location = root;
    m_query = query;
    m_searchHidden = includeHidden;
    m_searching = true;
    m_loading = false;
    m_refreshTimer.stop();
    clearThumbnails();
    watch({}); // results come from everywhere below; nothing to watch
    const quint64 generation = ++m_generation;

    beginResetModel();
    m_root.entries.clear();
    m_root.expanded.clear();
    m_root.path.clear();
    endResetModel();

    const search::Options options {query, includeHidden, 5000};
    // Not joined when stopped: on a phone or network share, the search may be stuck reading a
    // folder for a long time. It is told to stop, and what it still finds is dropped.
    const auto deliver = [model = QPointer(this), generation, stop = m_searchStop.get_token()](auto&& action) {
        if (stop.stop_requested())
            return;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [model, generation, action = std::forward<decltype(action)>(action)]() mutable {
                if (model && generation == model->m_generation)
                    action(*model);
            },
            Qt::QueuedConnection);
    };
    std::thread([deliver, root, options, stop = m_searchStop.get_token()] {
        search::run(root, options, stop, [&](std::vector<FileEntry> batch) {
            deliver([batch = std::move(batch)](DirectoryModel& model) mutable { model.append(std::move(batch)); });
        });
        deliver([](DirectoryModel& model) {
            model.m_searching = false;
            emit model.searchFinished();
        });
    }).detach();
}

int DirectoryModel::rowOf(const QString& name) const
{
    return rowIn(m_root, name);
}

int DirectoryModel::rowIn(const Folder& folder, const QString& name)
{
    for (std::size_t row = 0; row < folder.entries.size(); ++row) {
        if (folder.entries[row].name == name)
            return static_cast<int>(row);
    }
    return -1;
}

void DirectoryModel::setExpandableFolders(bool expandable)
{
    if (expandable == m_expandable)
        return;
    beginResetModel();
    m_expandable = expandable;
    m_root.expanded.clear();
    watch(m_root.path);
    endResetModel();
}

DirectoryModel::Folder* DirectoryModel::folderOf(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Folder*>(index.internalPointer()) : nullptr;
}

const FileEntry& DirectoryModel::entry(const QModelIndex& index) const
{
    return folderOf(index)->entries[static_cast<std::size_t>(index.row())];
}

DirectoryModel::Folder* DirectoryModel::childFolder(const QModelIndex& index) const
{
    if (!index.isValid())
        return &m_root;
    const auto it = folderOf(index)->expanded.find(entry(index).name);
    return it == folderOf(index)->expanded.end() ? nullptr : it->second.get();
}

QModelIndex DirectoryModel::indexOf(const Folder& folder) const
{
    if (!folder.parent)
        return {};
    const int row = rowIn(*folder.parent, folder.name);
    return row < 0 ? QModelIndex() : createIndex(row, 0, folder.parent);
}

DirectoryModel::Folder* DirectoryModel::findFolder(const QString& path) const
{
    if (path == m_root.path)
        return &m_root;
    const QString prefix = m_root.path.endsWith(u'/') ? m_root.path : m_root.path + u'/';
    if (m_root.path.isEmpty() || !path.startsWith(prefix))
        return nullptr;
    Folder* folder = &m_root;
    for (const QString& name : path.mid(prefix.size()).split(u'/', Qt::SkipEmptyParts)) {
        const auto it = folder->expanded.find(name);
        if (it == folder->expanded.end())
            return nullptr;
        folder = it->second.get();
    }
    return folder;
}

QModelIndex DirectoryModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() && parent.column() != NameColumn)
        return {};
    Folder* folder = childFolder(parent);
    if (!folder || row < 0 || row >= static_cast<int>(folder->entries.size()) || column < 0 || column >= ColumnCount)
        return {};
    return createIndex(row, column, folder);
}

QModelIndex DirectoryModel::parent(const QModelIndex& child) const
{
    const Folder* folder = folderOf(child);
    return folder ? indexOf(*folder) : QModelIndex();
}

bool DirectoryModel::hasChildren(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return !m_root.entries.empty();
    if (parent.column() != NameColumn || !m_expandable || !m_query.isEmpty())
        return false;
    const FileEntry& e = entry(parent);
    return e.isDir && !location::isTrash(e.url);
}

bool DirectoryModel::canFetchMore(const QModelIndex& parent) const
{
    if (!parent.isValid() || !hasChildren(parent))
        return false;
    const Folder* folder = childFolder(parent);
    return !folder || (!folder->listed && !folder->listing);
}

void DirectoryModel::fetchMore(const QModelIndex& parent)
{
    if (!canFetchMore(parent))
        return;
    Folder* container = folderOf(parent);
    const FileEntry& e = entry(parent);
    auto& slot = container->expanded[e.name];
    if (!slot) {
        slot = std::make_unique<Folder>();
        slot->parent = container;
        slot->name = e.name;
        slot->path = e.path;
        slot->url = e.url;
    }
    listFolder(*slot, false);
}

void DirectoryModel::listFolder(Folder& folder, bool refresh)
{
    folder.listing = true;
    if (!refresh && QFileInfo(folder.path).isDir()) // folders inside archives are not on disk
        m_watcher.addPath(folder.path);
    const quint64 generation = m_generation;
    const QString path = folder.path;
    const QUrl url = folder.url;
    auto* watcher = new QFutureWatcher<ListingResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, path] {
        watcher->deleteLater();
        // The folder may have been collapsed away, or another one shown meanwhile.
        Folder* folder = generation == m_generation ? findFolder(path) : nullptr;
        if (!folder || folder == &m_root)
            return;
        folder->listing = false;
        ListingResult result = watcher->future().takeResult();
        if (!result)
            return; // shown as empty; what went wrong shows when it is opened
        if (folder->listed) {
            merge(*folder, std::move(*result));
            return;
        }
        folder->listed = true;
        if (result->empty())
            return;
        beginInsertRows(indexOf(*folder), 0, static_cast<int>(result->size()) - 1);
        folder->entries = std::move(*result);
        endInsertRows();
    });
    watcher->setFuture(QtConcurrent::run([url] { return listDirectory(url); }));
}

void DirectoryModel::refreshExpanded()
{
    for (const QString& path : std::exchange(m_changedFolders, {})) {
        if (Folder* folder = findFolder(path); folder && folder != &m_root && folder->listed && !folder->listing)
            listFolder(*folder, true);
    }
}

void DirectoryModel::forgetExpanded(Folder& folder)
{
    for (auto& [name, child] : folder.expanded)
        forgetExpanded(*child);
    if (&folder != &m_root)
        m_watcher.removePath(folder.path);
}

void DirectoryModel::append(std::vector<FileEntry> entries)
{
    if (entries.empty())
        return;
    const int first = rowCount();
    beginInsertRows({}, first, first + static_cast<int>(entries.size()) - 1);
    std::ranges::move(entries, std::back_inserter(m_root.entries));
    endInsertRows();
}

void DirectoryModel::startListing(bool refresh)
{
    if (!refresh) {
        ++m_generation;
        m_loading = true;
        emit loadingStarted();
    }
    // A gradual listing still going on is as fresh as a new one would be.
    if (refresh && m_remoteListing && m_loading)
        return;
    stopRemoteListing();
    if (location::isLocal(m_location) && gvfs::isGvfsPath(location::localPath(m_location)) && gvfs::isAvailable()) {
        startRemoteListing(refresh);
        return;
    }

    const quint64 generation = m_generation;
    const QUrl location = m_location;
    auto* watcher = new QFutureWatcher<ListingResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, refresh] {
        watcher->deleteLater();
        if (generation == m_generation)
            finishListing(watcher->future().takeResult(), refresh);
    });
    watcher->setFuture(QtConcurrent::run([location] { return listDirectory(location); }));
}

void DirectoryModel::stopRemoteListing()
{
    if (!m_remoteListing)
        return;
    // This may be running inside one of its signals, as when a failure leads to going elsewhere.
    m_remoteListing->disconnect(this);
    m_remoteListing.release()->deleteLater();
}

void DirectoryModel::startRemoteListing(bool refresh)
{
    m_remoteListing = std::make_unique<gvfs::Listing>(m_location);
    gvfs::Listing* listing = m_remoteListing.get();
    // On a refresh the entries are merged at the end, like after any other listing; on the
    // first load they are shown as they come.
    if (!refresh) {
        connect(listing, &gvfs::Listing::found, this, &DirectoryModel::append);
    }
    connect(
        listing, &gvfs::Listing::finished, this, [this, listing, refresh](const std::expected<void, QString>& result) {
            if (!result) {
                m_loading = false;
                emit loadFailed(result.error());
                return;
            }
            if (refresh) {
                finishListing(listing->entries(), true);
                return;
            }
            m_loading = false;
            emit loaded();
        });
    listing->start();
}

void DirectoryModel::finishListing(ListingResult result, bool refresh)
{
    if (!result) {
        m_loading = false;
        emit loadFailed(result.error());
        return;
    }

    if (refresh && !m_loading) {
        merge(m_root, std::move(*result));
        return;
    }

    beginResetModel();
    forgetExpanded(m_root);
    m_root.expanded.clear();
    m_root.entries = std::move(*result);
    endResetModel();
    m_loading = false;
    emit loaded();
}

void DirectoryModel::merge(Folder& folder, std::vector<FileEntry> fresh)
{
    const QModelIndex parent = indexOf(folder);
    if (&folder != &m_root && !parent.isValid())
        return;
    std::vector<FileEntry>& entries = folder.entries;

    QHash<QString, std::size_t> freshRows;
    freshRows.reserve(static_cast<qsizetype>(fresh.size()));
    for (std::size_t i = 0; i < fresh.size(); ++i)
        freshRows.insert(fresh[i].name, i);

    for (int row = static_cast<int>(entries.size()) - 1; row >= 0; --row) {
        const QString name = entries[static_cast<std::size_t>(row)].name;
        if (freshRows.contains(name))
            continue;
        beginRemoveRows(parent, row, row);
        entries.erase(entries.begin() + row);
        if (const auto it = folder.expanded.find(name); it != folder.expanded.end()) {
            forgetExpanded(*it->second);
            folder.expanded.erase(it);
        }
        endRemoveRows();
    }

    QHash<QString, int> currentRows;
    for (std::size_t row = 0; row < entries.size(); ++row)
        currentRows.insert(entries[row].name, static_cast<int>(row));

    std::vector<FileEntry> added;
    for (FileEntry& entry : fresh) {
        const auto it = currentRows.constFind(entry.name);
        if (it == currentRows.cend()) {
            added.push_back(std::move(entry));
            continue;
        }
        FileEntry& current = entries[static_cast<std::size_t>(*it)];
        if (current != entry) {
            if (current.modified != entry.modified || current.size != entry.size) {
                m_thumbnails.remove(current.path);
                m_thumbnailsRequested.remove(current.path);
            }
            current = std::move(entry);
            emit dataChanged(index(*it, 0, parent), index(*it, ColumnCount - 1, parent));
        }
    }

    if (!added.empty()) {
        const int first = static_cast<int>(entries.size());
        beginInsertRows(parent, first, first + static_cast<int>(added.size()) - 1);
        std::ranges::move(added, std::back_inserter(entries));
        endInsertRows();
    }
}

void DirectoryModel::watch(const QString& path)
{
    if (const QStringList watched = m_watcher.directories(); !watched.isEmpty())
        m_watcher.removePaths(watched);
    if (QFileInfo(path).isDir())
        m_watcher.addPath(path);
}

QIcon DirectoryModel::icon(const FileEntry& entry) const
{
    QString primary;
    QString fallback;
    if (entry.isDir) {
        primary = location::directoryIconName(entry.path);
        fallback = u"folder"_s;
        if (primary.isEmpty())
            primary = fallback;
    } else {
        primary = m_iconOverrides.value(entry.mimeType, entry.iconName);
        fallback = entry.genericIconName;
    }

    const auto cached = m_iconCache.constFind(primary);
    if (cached != m_iconCache.cend())
        return *cached;

    QIcon icon;
    for (const QString& name : {primary, fallback, u"text-x-generic"_s, u"unknown"_s}) {
        if (!name.isEmpty() && QIcon::hasThemeIcon(name)) {
            icon = QIcon::fromTheme(name);
            break;
        }
    }
    m_iconCache.insert(primary, icon);
    return icon;
}

void DirectoryModel::setIconOverrides(const QHash<QString, QString>& overrides)
{
    m_iconOverrides = builtinIconOverrides;
    m_iconOverrides.insert(overrides);
    m_iconCache.clear();
    if (!m_root.entries.empty())
        emit dataChanged(index(0, NameColumn), index(rowCount() - 1, NameColumn), {Qt::DecorationRole});
}

void DirectoryModel::setThumbnailSize(int pixels)
{
    const int previous = m_thumbnailer.size();
    m_thumbnailer.setSize(pixels);
    if (m_thumbnailer.size() == previous)
        return;
    clearThumbnails();
    if (!m_root.entries.empty())
        emit dataChanged(index(0, NameColumn), index(rowCount() - 1, NameColumn), {Qt::DecorationRole, ThumbnailRole});
}

const QPixmap* DirectoryModel::thumbnail(const FileEntry& entry) const
{
    // Thumbnails would read whole files over USB or the network, like Nautilus avoids by default.
    if (entry.isDir || entry.isRemote)
        return nullptr;
    if (const auto it = m_thumbnails.constFind(entry.path); it != m_thumbnails.cend())
        return &*it;
    // Views only ask for what they show, so only visible files get thumbnails made.
    if (!m_thumbnailsRequested.contains(entry.path)
        && m_thumbnailer.canThumbnail(entry.path, entry.mimeType, entry.size)) {
        m_thumbnailsRequested.insert(entry.path);
        m_thumbnailer.request({entry.path, entry.mimeType, entry.modified, entry.size});
    }
    return nullptr;
}

void DirectoryModel::thumbnailReady(const QString& path, const QImage& image)
{
    if (!m_thumbnailsRequested.contains(path))
        return;
    m_thumbnails.insert(path, QPixmap::fromImage(image));
    // Search results come from anywhere; listings from the folder shown or one expanded in it.
    const QFileInfo info(path);
    QModelIndex changed;
    if (!m_query.isEmpty()) {
        for (std::size_t row = 0; row < m_root.entries.size(); ++row) {
            if (m_root.entries[row].path == path)
                changed = index(static_cast<int>(row), NameColumn);
        }
    } else if (const Folder* folder = findFolder(info.absolutePath())) {
        const int row = rowIn(*folder, info.fileName());
        if (row >= 0)
            changed = index(row, NameColumn, indexOf(*folder));
    }
    if (changed.isValid())
        emit dataChanged(changed, changed, {Qt::DecorationRole, ThumbnailRole});
}

void DirectoryModel::clearThumbnails()
{
    m_thumbnailer.cancelPending();
    m_thumbnails.clear();
    m_thumbnailsRequested.clear();
}

int DirectoryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() && parent.column() != NameColumn)
        return 0;
    const Folder* folder = childFolder(parent);
    return folder ? static_cast<int>(folder->entries.size()) : 0;
}

int DirectoryModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant DirectoryModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const FileEntry& e = entry(index);

    switch (role) {
    case Qt::EditRole:
        if (index.column() == NameColumn)
            return e.name;
        break;
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            return e.name;
        case SizeColumn:
            return formatSize(e);
        case TypeColumn:
            return e.mimeComment;
        case ModifiedColumn:
            return formatModified(e.modified);
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == NameColumn) {
            if (const QPixmap* pixmap = thumbnail(e))
                return QIcon(*pixmap);
            return icon(e);
        }
        break;
    case ThumbnailRole:
        if (const QPixmap* pixmap = thumbnail(e))
            return *pixmap;
        break;
    case Qt::ToolTipRole:
        if (index.column() == ModifiedColumn)
            return QLocale().toString(e.modified, QLocale::LongFormat);
        break;
    case UrlRole:
        return e.url;
    case PathRole:
        return e.path;
    case IsDirRole:
        return e.isDir;
    case HiddenRole:
        return e.isHidden;
    }
    return {};
}

QVariant DirectoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case NameColumn:
        return u"Name"_s;
    case SizeColumn:
        return u"Size"_s;
    case TypeColumn:
        return u"Type"_s;
    case ModifiedColumn:
        return u"Modified"_s;
    }
    return {};
}

Qt::ItemFlags DirectoryModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!hasChildren(index))
        flags |= Qt::ItemNeverHasChildren;
    // What is in an archive is not on disk: it cannot be dragged out as files, nor renamed.
    const QUrl url = entry(index).url;
    if (location::isArchive(url))
        return flags;
    flags |= Qt::ItemIsDragEnabled;
    // Names are edited in place; things in the trash keep theirs.
    if (index.column() == NameColumn && !location::isTrash(url))
        flags |= Qt::ItemIsEditable;
    return flags;
}

bool DirectoryModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || index.column() != NameColumn || !checkIndex(index, CheckIndexOption::IndexIsValid))
        return false;
    // The window does the renaming, with its checks and undo; the listing picks it up.
    const FileEntry& e = entry(index);
    if (value.toString() != e.name)
        emit renameRequested(e.path, value.toString());
    return false;
}

QStringList DirectoryModel::mimeTypes() const
{
    return {u"text/uri-list"_s};
}

QMimeData* DirectoryModel::mimeData(const QModelIndexList& indexes) const
{
    QList<QUrl> urls;
    for (const QModelIndex& index : indexes) {
        if (index.column() == NameColumn)
            urls << QUrl::fromLocalFile(entry(index).path);
    }
    auto data = std::make_unique<QMimeData>();
    data->setUrls(urls);
    return data.release(); // the view takes it
}

Qt::DropActions DirectoryModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
}

} // namespace ariadne
