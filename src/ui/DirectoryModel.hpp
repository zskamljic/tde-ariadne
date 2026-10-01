#pragma once

#include "core/DirectoryListing.hpp"
#include "core/Gvfs.hpp"
#include "core/Thumbnailer.hpp"

#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ariadne {

// The contents of one directory, and of the folders expanded in it in the list view. Listing
// happens on a worker thread; changes on disk are merged in place, so selection, scroll
// position and expanded folders survive them.
class DirectoryModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column { NameColumn, SizeColumn, TypeColumn, ModifiedColumn, ColumnCount };
    enum Role { UrlRole = Qt::UserRole + 1, PathRole, IsDirRole, HiddenRole, ThumbnailRole };

    explicit DirectoryModel(QObject* parent = nullptr);
    ~DirectoryModel() override;

    QUrl location() const { return m_location; }
    void setLocation(const QUrl& url);
    // Lists what matches `query` anywhere below `root` instead, as it is found.
    void setSearch(const QUrl& root, const QString& query, bool includeHidden);
    // Loads the folder again, or runs the search again.
    void reload();
    bool isLoading() const { return m_loading; }
    // Whether entries show up while the folder is still loading (phones and network locations).
    bool loadsGradually() const { return m_remoteListing != nullptr; }
    const QString& searchQuery() const { return m_query; }
    bool isSearching() const { return m_searching; }

    // Icon names to use for MIME types instead of the theme's, on top of the built-in ones.
    void setIconOverrides(const QHash<QString, QString>& overrides);
    // Largest edge, in device pixels, the thumbnails are shown at.
    void setThumbnailSize(int pixels);

    // Whether folders can be expanded in place, showing what they hold.
    void setExpandableFolders(bool expandable);

    // Entries of the folder shown, by row.
    const FileEntry& entry(int row) const { return m_root.entries[static_cast<std::size_t>(row)]; }
    int rowOf(const QString& name) const;
    // Any entry, also inside expanded folders.
    const FileEntry& entry(const QModelIndex& index) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override;

signals:
    void loadingStarted();
    void loaded();
    void loadFailed(const QString& message);
    void searchFinished();
    // A name was edited in a view.
    void renameRequested(const QString& path, const QString& newName);

private:
    // A listed folder: the one shown, or one expanded inside it.
    struct Folder {
        Folder* parent = nullptr; // null for the folder shown
        QString name; // of its entry in the parent
        QString path;
        QUrl url;
        std::vector<FileEntry> entries;
        std::unordered_map<QString, std::unique_ptr<Folder>> expanded; // by entry name
        bool listed = false;
        bool listing = false;
    };

    Folder* folderOf(const QModelIndex& index) const;
    // The listing of the folder `index` stands for, if it was expanded.
    Folder* childFolder(const QModelIndex& index) const;
    QModelIndex indexOf(const Folder& folder) const;
    static int rowIn(const Folder& folder, const QString& name);
    // The listed folder at `path`, if there is one.
    Folder* findFolder(const QString& path) const;
    void listFolder(Folder& folder, bool refresh);
    void refreshExpanded();
    void forgetExpanded(Folder& folder);

    void startListing(bool refresh);
    void finishListing(ListingResult result, bool refresh);
    void startRemoteListing(bool refresh);
    void stopRemoteListing();
    void append(std::vector<FileEntry> entries);
    void merge(Folder& folder, std::vector<FileEntry> fresh);
    void watch(const QString& path);
    QIcon icon(const FileEntry& entry) const;
    // The thumbnail for `entry` if there is one yet; asks for it otherwise.
    const QPixmap* thumbnail(const FileEntry& entry) const;
    void thumbnailReady(const QString& path, const QImage& image);
    void clearThumbnails();
    void stopSearch();

    QUrl m_location;
    mutable Folder m_root;
    bool m_expandable = true;
    QSet<QString> m_changedFolders; // expanded folders to list again
    QTimer m_expandedRefreshTimer;
    quint64 m_generation = 0;
    bool m_loading = false;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshTimer;
    mutable QHash<QString, QIcon> m_iconCache;
    QHash<QString, QString> m_iconOverrides;
    mutable Thumbnailer m_thumbnailer;
    QHash<QString, QPixmap> m_thumbnails; // by path
    mutable QSet<QString> m_thumbnailsRequested; // by path
    QString m_query;
    bool m_searchHidden = false;
    bool m_searching = false;
    std::unique_ptr<gvfs::Listing> m_remoteListing;
    std::stop_source m_searchStop;
};

} // namespace ariadne
