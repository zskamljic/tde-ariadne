#include "Sidebar.hpp"

#include "Dialog.hpp"
#include "DragDrop.hpp"
#include "Theme.hpp"
#include "core/Bookmarks.hpp"
#include "core/DeviceMonitor.hpp"
#include "core/Location.hpp"

#include <QContextMenuEvent>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

#include <memory>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr int RowHeight = 34;
constexpr int SeparatorHeight = 13;
constexpr int SideMargin = 6;

Sidebar::Kind kindOf(const QModelIndex& index)
{
    return static_cast<Sidebar::Kind>(index.data(Sidebar::KindRole).toInt());
}

Sidebar::Kind kindOf(const QListWidgetItem* item)
{
    return static_cast<Sidebar::Kind>(item->data(Sidebar::KindRole).toInt());
}

QRect ejectRect(const QRect& itemRect)
{
    const QRect row = itemRect.adjusted(SideMargin, 1, -SideMargin, -1);
    return QRect(row.right() - 30, row.top() + (row.height() - 26) / 2, 26, 26);
}

QIcon firstIcon(std::initializer_list<QString> names)
{
    for (const QString& name : names) {
        if (const QIcon icon = theme::symbolicIcon(name); !icon.isNull())
            return icon;
    }
    return {};
}

class SidebarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        return {0, kindOf(index) == Sidebar::Kind::Separator ? SeparatorHeight : RowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const auto& colors = theme::colors();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        if (kindOf(index) == Sidebar::Kind::Separator) {
            const int y = option.rect.center().y();
            painter->setPen(colors.border);
            painter->drawLine(option.rect.left() + 12, y, option.rect.right() - 12, y);
            painter->restore();
            return;
        }

        const QRect row = option.rect.adjusted(SideMargin, 1, -SideMargin, -1);
        const bool selected = option.state & QStyle::State_Selected;
        if (selected || (option.state & QStyle::State_MouseOver)) {
            QPainterPath path;
            path.addRoundedRect(QRectF(row), theme::radius(), theme::radius());
            painter->fillPath(path, selected ? colors.accent : colors.hover);
        }

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(row.left() + 10, row.top() + (row.height() - 16) / 2, 16, 16);
        icon.paint(painter, iconRect, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);

        const bool ejectable = index.data(Sidebar::EjectableRole).toBool();
        QRect textRect = row.adjusted(iconRect.right() - row.left() + 12, 0, ejectable ? -36 : -8, 0);
        painter->setPen(selected ? colors.accentText : colors.sidebarText);
        const QString text
            = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

        if (ejectable) {
            const QRect button = ejectRect(option.rect);
            QRect ejectIcon(0, 0, 16, 16);
            ejectIcon.moveCenter(button.center());
            theme::symbolicIcon(u"media-eject"_s)
                .paint(painter, ejectIcon, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);
        }
        painter->restore();
    }
};

} // namespace

Sidebar::Sidebar(Bookmarks& bookmarks, DeviceMonitor& devices, QWidget* parent)
    : QListWidget(parent)
    , m_bookmarks(bookmarks)
    , m_devices(devices)
{
    setObjectName(u"Sidebar"_s);
    setItemDelegate(new SidebarDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::NoFocus);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setSpacing(0);
    setContentsMargins(0, 6, 0, 6);

    watchTrash();
    connect(&m_bookmarks, &Bookmarks::changed, this, &Sidebar::rebuild);
    connect(&m_devices, &DeviceMonitor::changed, this, &Sidebar::rebuild);
    connect(&m_trashWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        watchTrash();
        rebuild();
    });

    rebuild();
}

void Sidebar::watchTrash()
{
    // The trash folder only exists once something was trashed; until then, watch the
    // closest existing parent to notice it being created.
    QString path = location::trashFilesPath();
    while (!QFileInfo(path).isDir() && path.count(u'/') > 1)
        path = QFileInfo(path).absolutePath();
    if (m_trashWatcher.directories() == QStringList {path})
        return;
    if (!m_trashWatcher.directories().isEmpty())
        m_trashWatcher.removePaths(m_trashWatcher.directories());
    m_trashWatcher.addPath(path);
}

void Sidebar::setCurrentLocation(const QUrl& url)
{
    m_current = url;
    const QSignalBlocker blocker(this);
    for (int row = 0; row < count(); ++row) {
        QListWidgetItem* candidate = item(row);
        if (kindOf(candidate) != Kind::Separator && candidate->data(UrlRole).toUrl() == url) {
            setCurrentItem(candidate);
            return;
        }
    }
    clearSelection();
    setCurrentItem(nullptr);
}

void Sidebar::rebuild()
{
    m_pressedItem = nullptr;
    const QSignalBlocker blocker(this);
    clear();

    addEntry(Kind::Place, u"Home"_s, u"user-home"_s, location::home());
    const bool trashFull
        = !QDir(location::trashFilesPath()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    addEntry(Kind::Place, u"Trash"_s, trashFull ? u"user-trash-full"_s : u"user-trash"_s, location::trash());

    addSeparator();
    const auto& bookmarks = m_bookmarks.items();
    for (qsizetype i = 0; i < bookmarks.size(); ++i) {
        const Bookmark& bookmark = bookmarks[i];
        if (!bookmark.url.isLocalFile())
            continue; // remote bookmarks are kept in the file, but cannot be opened yet
        const QString path = QDir::cleanPath(bookmark.url.toLocalFile());
        const QString special = location::directoryIconName(path);
        QListWidgetItem* item = addEntry(Kind::Bookmark, Bookmarks::displayName(bookmark),
            special.isEmpty() ? u"folder"_s : special, location::fromLocalPath(path));
        item->setData(BookmarkIndexRole, i);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
    }

    addSeparator();
    addEntry(Kind::Device, u"File System"_s, u"drive-harddisk"_s, location::root());
    for (const Device& device : m_devices.devices()) {
        const QUrl url = device.mountPoint.isEmpty() ? QUrl() : location::fromLocalPath(device.mountPoint);
        QListWidgetItem* item = addEntry(Kind::Device, device.label, device.iconName, url);
        item->setData(DeviceRole, device.id);
        item->setData(EjectableRole, !device.mountPoint.isEmpty() && device.removable);
        if (device.locked) {
            item->setIcon(firstIcon({u"changes-prevent"_s, u"channel-secure"_s, device.iconName}));
            item->setToolTip(u"%1 (locked)"_s.arg(device.label));
        } else if (device.mountPoint.isEmpty()) {
            item->setToolTip(u"%1 (not mounted)"_s.arg(device.label));
        } else {
            item->setToolTip(device.uri.isEmpty() ? device.mountPoint : device.uri);
        }
    }

    setCurrentLocation(m_current);
}

QListWidgetItem* Sidebar::addEntry(Kind kind, const QString& label, const QString& iconName, const QUrl& url)
{
    auto* item = new QListWidgetItem(this);
    item->setText(label);
    item->setIcon(firstIcon({iconName, u"folder"_s}));
    item->setData(KindRole, static_cast<int>(kind));
    item->setData(UrlRole, url);
    item->setToolTip(url.isLocalFile() ? url.toLocalFile() : label);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

void Sidebar::addSeparator()
{
    auto* item = new QListWidgetItem(this);
    item->setData(KindRole, static_cast<int>(Kind::Separator));
    item->setFlags(Qt::NoItemFlags);
}

void Sidebar::activate(QListWidgetItem* item, bool newWindow)
{
    if (!item || kindOf(item) == Kind::Separator)
        return;

    const QUrl url = item->data(UrlRole).toUrl();
    if (url.isEmpty()) {
        emit mountRequested(item->data(DeviceRole).toString(), newWindow);
        return;
    }
    newWindow ? emit newWindowRequested(url) : emit locationRequested(url);
}

bool Sidebar::isEjectHit(QListWidgetItem* item, const QPoint& position) const
{
    return item && item->data(EjectableRole).toBool() && ejectRect(visualItemRect(item)).contains(position);
}

void Sidebar::mousePressEvent(QMouseEvent* event)
{
    QListWidgetItem* item = itemAt(event->position().toPoint());
    m_pressedItem = item;
    if (event->button() == Qt::LeftButton && !isEjectHit(item, event->position().toPoint()))
        QListWidget::mousePressEvent(event);
    else
        event->accept();
}

void Sidebar::mouseReleaseEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    QListWidgetItem* item = itemAt(position);
    const bool sameItem = item && item == m_pressedItem;
    m_pressedItem = nullptr;

    if (event->button() == Qt::LeftButton) {
        QListWidget::mouseReleaseEvent(event);
        if (!sameItem)
            return;
        if (isEjectHit(item, position))
            emit ejectRequested(item->data(DeviceRole).toString());
        else
            activate(item, false);
        setCurrentLocation(m_current); // the location change decides the highlight
    } else if (event->button() == Qt::MiddleButton && sameItem) {
        activate(item, true);
    }
    event->accept();
}

void Sidebar::contextMenuEvent(QContextMenuEvent* event)
{
    QListWidgetItem* item = itemAt(event->pos());
    if (!item || kindOf(item) == Kind::Separator)
        return;

    const QUrl url = item->data(UrlRole).toUrl();
    const QString deviceId = item->data(DeviceRole).toString();
    QMenu menu(this);

    if (!url.isEmpty()) {
        menu.addAction(u"Open"_s, this, [this, url] { emit locationRequested(url); });
        menu.addAction(u"Open in New Window"_s, this, [this, url] { emit newWindowRequested(url); });
    } else {
        const bool locked
            = m_devices.find(deviceId).transform([](const Device& d) { return d.locked; }).value_or(false);
        menu.addAction(
            locked ? u"Unlock…"_s : u"Mount"_s, this, [this, deviceId] { emit mountRequested(deviceId, false); });
    }

    if (url == location::trash()) {
        menu.addSeparator();
        menu.addAction(u"Empty Trash…"_s, this, [this] { emit emptyTrashRequested(); });
    }

    if (kindOf(item) == Kind::Bookmark) {
        menu.addSeparator();
        menu.addAction(u"Rename…"_s, this, [this, url, name = item->text()] {
            if (const auto label = Dialog::getText(this, u"Rename Bookmark"_s, u"Name"_s, name, u"Rename"_s))
                m_bookmarks.rename(url, *label);
        });
        menu.addAction(u"Remove from Bookmarks"_s, this, [this, url] { m_bookmarks.remove(url); });
    }

    if (!deviceId.isEmpty() && !url.isEmpty()) {
        menu.addSeparator();
        menu.addAction(u"Unmount"_s, this, [this, deviceId] { emit unmountRequested(deviceId); });
        if (const auto device = m_devices.find(deviceId); device && device->removable && device->uri.isEmpty())
            menu.addAction(u"Eject"_s, this, [this, deviceId] { emit ejectRequested(deviceId); });
    }

    menu.exec(event->globalPos());
}

void Sidebar::startDrag(Qt::DropActions)
{
    QListWidgetItem* item = currentItem();
    if (!item || kindOf(item) != Kind::Bookmark)
        return;

    m_draggedBookmark = item->data(BookmarkIndexRole).toLongLong();
    auto mime = std::make_unique<QMimeData>();
    mime->setUrls({item->data(UrlRole).toUrl()});
    auto* drag = new QDrag(this); // deletes itself once the drag is over
    drag->setMimeData(mime.release());
    drag->setPixmap(item->icon().pixmap(QSize(24, 24), devicePixelRatioF()));
    drag->exec(Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::MoveAction);
    m_draggedBookmark = -1;
    setCurrentLocation(m_current);
}

Sidebar::Drop Sidebar::dropFor(const QDropEvent* event) const
{
    const QPoint position = event->position().toPoint();
    if (event->source() == this) {
        if (m_draggedBookmark < 0)
            return {};
        const DropTarget target = dropTarget(position);
        return {Drop::Kind::Reorder, target.bookmarkIndex, target.indicatorY, -1, {}, Qt::MoveAction};
    }

    const QStringList paths = dnd::localPaths(event->mimeData());
    if (paths.isEmpty())
        return {};

    // The top and bottom edges of a bookmark are for adding bookmarks between the others;
    // the rest of any place accepts the files themselves.
    QListWidgetItem* item = itemAt(position);
    const bool onItem = item && kindOf(item) != Kind::Separator;
    bool onEdge = false;
    if (onItem && kindOf(item) == Kind::Bookmark) {
        const QRect rect = visualItemRect(item);
        const int offset = position.y() - rect.top();
        onEdge = offset < rect.height() / 4 || offset > rect.height() * 3 / 4;
    }
    if (onItem && !onEdge) {
        const QUrl url = item->data(UrlRole).toUrl();
        if (url == location::trash()) {
            if (event->possibleActions() & Qt::MoveAction)
                return {Drop::Kind::Trash, -1, -1, row(item), {}, Qt::MoveAction};
            return {};
        }
        if (url.isLocalFile()) {
            const QString directory = location::localPath(url);
            const Qt::DropAction action = dnd::choose(paths, directory, event->modifiers(), event->possibleActions());
            if (action != Qt::IgnoreAction)
                return {Drop::Kind::Into, -1, -1, row(item), directory, action};
        }
        if (kindOf(item) != Kind::Bookmark)
            return {};
    }

    if (std::ranges::any_of(paths, [](const QString& path) { return QFileInfo(path).isDir(); })) {
        const DropTarget target = dropTarget(position);
        const Qt::DropAction action = (event->possibleActions() & Qt::LinkAction) ? Qt::LinkAction : Qt::CopyAction;
        return {Drop::Kind::Bookmark, target.bookmarkIndex, target.indicatorY, -1, {}, action};
    }
    return {};
}

Sidebar::DropTarget Sidebar::dropTarget(const QPoint& position) const
{
    const QListWidgetItem* last = nullptr;
    for (int row = 0; row < count(); ++row) {
        const QListWidgetItem* candidate = item(row);
        if (kindOf(candidate) != Kind::Bookmark)
            continue;
        const QRect rect = visualItemRect(candidate);
        if (position.y() < rect.center().y())
            return {candidate->data(BookmarkIndexRole).toLongLong(), rect.top()};
        last = candidate;
    }
    if (last)
        return {last->data(BookmarkIndexRole).toLongLong() + 1, visualItemRect(last).bottom() + 1};

    // No bookmarks yet: show the indicator on the separator between the sections.
    for (int row = 0; row < count(); ++row) {
        if (kindOf(item(row)) == Kind::Separator)
            return {m_bookmarks.items().size(), visualItemRect(item(row)).center().y()};
    }
    return {m_bookmarks.items().size(), 0};
}

void Sidebar::showDrop(const Drop& drop)
{
    const bool between = drop.kind == Drop::Kind::Bookmark || drop.kind == Drop::Kind::Reorder;
    m_dropIndicatorY = between ? drop.indicatorY : -1;
    m_dropRow = between ? -1 : drop.row;
    viewport()->update();
}

void Sidebar::dragEnterEvent(QDragEnterEvent* event)
{
    dragMoveEvent(event);
    // Accept entering whenever files are dragged, so moves over other places still arrive.
    if (!event->isAccepted() && (event->source() == this || !dnd::localPaths(event->mimeData()).isEmpty())) {
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
    }
}

void Sidebar::dragMoveEvent(QDragMoveEvent* event)
{
    const Drop drop = dropFor(event);
    qCDebug(lcDnd) << "sidebar" << event->type() << "at" << event->position() << "kind" << int(drop.kind) << "into"
                   << drop.directory << "possible" << event->possibleActions() << "->" << drop.action;
    showDrop(drop);
    if (drop.kind == Drop::Kind::None) {
        event->ignore();
        return;
    }
    event->setDropAction(drop.action);
    event->accept();
}

void Sidebar::dragLeaveEvent(QDragLeaveEvent* event)
{
    showDrop({});
    event->accept();
}

void Sidebar::dropEvent(QDropEvent* event)
{
    const Drop drop = dropFor(event);
    showDrop({});
    if (drop.kind == Drop::Kind::None) {
        event->ignore();
        return;
    }
    event->setDropAction(drop.action);
    event->accept();

    const QStringList paths = dnd::localPaths(event->mimeData());
    switch (drop.kind) {
    case Drop::Kind::Reorder:
        // The move rebuilds the list, so do it once the drag has finished.
        QMetaObject::invokeMethod(
            this, [this, from = m_draggedBookmark, to = drop.bookmarkIndex] { m_bookmarks.move(from, to); },
            Qt::QueuedConnection);
        break;
    case Drop::Kind::Bookmark: {
        qsizetype index = drop.bookmarkIndex;
        for (const QString& path : paths) {
            const QUrl url = QUrl::fromLocalFile(path);
            if (QFileInfo(path).isDir() && !m_bookmarks.contains(url))
                m_bookmarks.add(url, index++);
        }
        break;
    }
    case Drop::Kind::Into:
        emit filesDropped(paths, drop.directory, drop.action);
        break;
    case Drop::Kind::Trash:
        emit trashDropped(paths);
        break;
    case Drop::Kind::None:
        break;
    }
}

void Sidebar::paintEvent(QPaintEvent* event)
{
    QListWidget::paintEvent(event);
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(theme::colors().accent, 2));
    if (m_dropIndicatorY >= 0)
        painter.drawLine(12, m_dropIndicatorY, viewport()->width() - 12, m_dropIndicatorY);
    if (m_dropRow >= 0) {
        const QRectF rect = QRectF(visualItemRect(item(m_dropRow))).adjusted(7, 2, -7, -2);
        painter.drawRoundedRect(rect, theme::radius(), theme::radius());
    }
}

} // namespace ariadne
