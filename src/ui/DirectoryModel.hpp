#pragma once

#include "core/DirectoryListing.hpp"
#include "core/Gvfs.hpp"
#include "core/Thumbnailer.hpp"

#include <QAbstractTableModel>
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
#include <vector>

namespace ariadne {

// The contents of one directory. Listing happens on a worker thread; changes on disk are
// merged in place, so selection and scroll position survive them.
class DirectoryModel : public QAbstractTableModel {
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

    const FileEntry& entry(int row) const { return m_entries[static_cast<std::size_t>(row)]; }
    int rowOf(const QString& name) const;

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
    void startListing(bool refresh);
    void finishListing(ListingResult result, bool refresh);
    void startRemoteListing(bool refresh);
    void stopRemoteListing();
    void append(std::vector<FileEntry> entries);
    void merge(std::vector<FileEntry> fresh);
    void watch(const QString& path);
    QIcon icon(const FileEntry& entry) const;
    // The thumbnail for `entry` if there is one yet; asks for it otherwise.
    const QPixmap* thumbnail(const FileEntry& entry) const;
    void thumbnailReady(const QString& path, const QImage& image);
    void clearThumbnails();
    void stopSearch();

    QUrl m_location;
    std::vector<FileEntry> m_entries;
    quint64 m_generation = 0;
    bool m_loading = false;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshTimer;
    mutable QHash<QString, QIcon> m_iconCache;
    QHash<QString, QString> m_iconOverrides;
    mutable Thumbnailer m_thumbnailer;
    QHash<QString, QPixmap> m_thumbnails; // by file name
    mutable QSet<QString> m_thumbnailsRequested; // by file name
    QString m_query;
    bool m_searchHidden = false;
    bool m_searching = false;
    std::unique_ptr<gvfs::Listing> m_remoteListing;
    std::stop_source m_searchStop;
};

} // namespace ariadne
