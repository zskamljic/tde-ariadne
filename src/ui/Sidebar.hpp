#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QListWidget>
#include <QTimer>
#include <QUrl>

namespace ariadne {

class Bookmarks;
class DeviceMonitor;

// Three sections: places (home, trash), bookmarks and disks. Folders dropped onto the
// sidebar become bookmarks; bookmarks can be reordered by dragging.
class Sidebar : public QListWidget {
    Q_OBJECT

public:
    enum class Kind { Place, Bookmark, Device, Separator };
    enum Role { KindRole = Qt::UserRole + 1, UrlRole, DeviceRole, BookmarkIndexRole, EjectableRole, UsageRole };

    Sidebar(Bookmarks& bookmarks, DeviceMonitor& devices, QWidget* parent = nullptr);

    void setCurrentLocation(const QUrl& url);

signals:
    void locationRequested(const QUrl& url);
    void newWindowRequested(const QUrl& url);
    void mountRequested(const QString& deviceId, bool newWindow);
    void unmountRequested(const QString& deviceId);
    void ejectRequested(const QString& deviceId);
    void emptyTrashRequested();
    void filesDropped(const QStringList& paths, const QString& directory, Qt::DropAction action);
    void trashDropped(const QStringList& paths);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct DropTarget {
        qsizetype bookmarkIndex;
        int indicatorY;
    };

    void rebuild();
    void measureUsage();
    void showUsage();
    void watchTrash();
    QListWidgetItem* addEntry(Kind kind, const QString& label, const QString& iconName, const QUrl& url);
    void addSeparator();
    void activate(QListWidgetItem* item, bool newWindow);
    bool isEjectHit(QListWidgetItem* item, const QPoint& position) const;
    // What dropping here would do.
    struct Drop {
        enum class Kind { None, Reorder, Bookmark, Into, Trash };
        Kind kind = Kind::None;
        qsizetype bookmarkIndex = -1;
        int indicatorY = -1;
        int row = -1;
        QString directory;
        Qt::DropAction action = Qt::IgnoreAction;
    };
    Drop dropFor(const QDropEvent* event) const;
    void showDrop(const Drop& drop);
    DropTarget dropTarget(const QPoint& position) const;

    Bookmarks& m_bookmarks;
    DeviceMonitor& m_devices;
    QFileSystemWatcher m_trashWatcher;
    struct StorageUsage {
        qint64 total = 0;
        qint64 available = 0;
    };
    // How full each mounted drive is, by mount point.
    QHash<QString, StorageUsage> m_usage;
    QTimer m_usageTimer;
    bool m_measuring = false;
    bool m_measureAgain = false;
    QUrl m_current;
    Kind m_currentKind = Kind::Place;
    QListWidgetItem* m_pressedItem = nullptr;
    qsizetype m_draggedBookmark = -1;
    int m_dropIndicatorY = -1;
    int m_dropRow = -1;
};

} // namespace ariadne
