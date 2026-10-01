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
    : QAbstractTableModel(parent)
    , m_iconOverrides(builtinIconOverrides)
{
    connect(&m_thumbnailer, &Thumbnailer::ready, this, &DirectoryModel::thumbnailReady);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(200);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { startListing(true); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_refreshTimer.start(); });
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
    m_entries.clear();
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
    m_entries.clear();
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
    for (std::size_t row = 0; row < m_entries.size(); ++row) {
        if (m_entries[row].name == name)
            return static_cast<int>(row);
    }
    return -1;
}

void DirectoryModel::append(std::vector<FileEntry> entries)
{
    if (entries.empty())
        return;
    const int first = rowCount();
    beginInsertRows({}, first, first + static_cast<int>(entries.size()) - 1);
    std::ranges::move(entries, std::back_inserter(m_entries));
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
    if (gvfs::isGvfsPath(location::localPath(m_location)) && gvfs::isAvailable()) {
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
        merge(std::move(*result));
        return;
    }

    beginResetModel();
    m_entries = std::move(*result);
    endResetModel();
    m_loading = false;
    emit loaded();
}

void DirectoryModel::merge(std::vector<FileEntry> fresh)
{
    QHash<QString, std::size_t> freshRows;
    freshRows.reserve(static_cast<qsizetype>(fresh.size()));
    for (std::size_t i = 0; i < fresh.size(); ++i)
        freshRows.insert(fresh[i].name, i);

    for (int row = static_cast<int>(m_entries.size()) - 1; row >= 0; --row) {
        if (!freshRows.contains(m_entries[static_cast<std::size_t>(row)].name)) {
            beginRemoveRows({}, row, row);
            m_entries.erase(m_entries.begin() + row);
            endRemoveRows();
        }
    }

    QHash<QString, int> currentRows;
    for (std::size_t row = 0; row < m_entries.size(); ++row)
        currentRows.insert(m_entries[row].name, static_cast<int>(row));

    std::vector<FileEntry> added;
    for (FileEntry& entry : fresh) {
        const auto it = currentRows.constFind(entry.name);
        if (it == currentRows.cend()) {
            added.push_back(std::move(entry));
            continue;
        }
        FileEntry& current = m_entries[static_cast<std::size_t>(*it)];
        if (current != entry) {
            if (current.modified != entry.modified || current.size != entry.size) {
                m_thumbnails.remove(current.name);
                m_thumbnailsRequested.remove(current.name);
            }
            current = std::move(entry);
            emit dataChanged(index(*it, 0), index(*it, ColumnCount - 1));
        }
    }

    if (!added.empty()) {
        const int first = static_cast<int>(m_entries.size());
        beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
        std::ranges::move(added, std::back_inserter(m_entries));
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
    if (!m_entries.empty())
        emit dataChanged(index(0, NameColumn), index(rowCount() - 1, NameColumn), {Qt::DecorationRole});
}

void DirectoryModel::setThumbnailSize(int pixels)
{
    const int previous = m_thumbnailer.size();
    m_thumbnailer.setSize(pixels);
    if (m_thumbnailer.size() == previous)
        return;
    clearThumbnails();
    if (!m_entries.empty())
        emit dataChanged(index(0, NameColumn), index(rowCount() - 1, NameColumn), {Qt::DecorationRole, ThumbnailRole});
}

const QPixmap* DirectoryModel::thumbnail(const FileEntry& entry) const
{
    // Thumbnails would read whole files over USB or the network, like Nautilus avoids by default.
    if (entry.isDir || entry.isRemote)
        return nullptr;
    if (const auto it = m_thumbnails.constFind(entry.name); it != m_thumbnails.cend())
        return &*it;
    // Views only ask for what they show, so only visible files get thumbnails made.
    if (!m_thumbnailsRequested.contains(entry.name)
        && m_thumbnailer.canThumbnail(entry.path, entry.mimeType, entry.size)) {
        m_thumbnailsRequested.insert(entry.name);
        m_thumbnailer.request({entry.path, entry.mimeType, entry.modified, entry.size});
    }
    return nullptr;
}

void DirectoryModel::thumbnailReady(const QString& path, const QImage& image)
{
    const int row = rowOf(QFileInfo(path).fileName());
    if (row < 0 || entry(row).path != path)
        return;
    m_thumbnails.insert(entry(row).name, QPixmap::fromImage(image));
    emit dataChanged(index(row, NameColumn), index(row, NameColumn), {Qt::DecorationRole, ThumbnailRole});
}

void DirectoryModel::clearThumbnails()
{
    m_thumbnailer.cancelPending();
    m_thumbnails.clear();
    m_thumbnailsRequested.clear();
}

int DirectoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int DirectoryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DirectoryModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const FileEntry& e = entry(index.row());

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
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren;
    // Names are edited in place; things in the trash keep theirs.
    if (index.column() == NameColumn && !location::isTrash(entry(index.row()).url))
        flags |= Qt::ItemIsEditable;
    return flags;
}

bool DirectoryModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || index.column() != NameColumn || !checkIndex(index, CheckIndexOption::IndexIsValid))
        return false;
    // The window does the renaming, with its checks and undo; the listing picks it up.
    const FileEntry& e = entry(index.row());
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
            urls << QUrl::fromLocalFile(entry(index.row()).path);
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
