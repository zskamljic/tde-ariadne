#include "FileView.hpp"

#include "DirectoryModel.hpp"
#include "DragDrop.hpp"
#include "GridView.hpp"

#include <tde/Theme.hpp>

#include <QApplication>
#include <QDropEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <memory>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr std::array GridZoomLevels {32, 48, 64, 96, 128, 192, 256};
constexpr std::array ListZoomLevels {16, 24, 32, 48, 64};

template <std::size_t N> int stepZoom(const std::array<int, N>& levels, int current, int steps)
{
    // Start from the level closest to the current size, which may come from the config.
    const auto closest = std::ranges::min_element(levels, {}, [&](int level) { return std::abs(level - current); });
    const auto index = std::clamp<std::ptrdiff_t>(closest - levels.begin() + steps, 0, N - 1);
    return levels[static_cast<std::size_t>(index)];
}

// The folder files would be dropped into, as marked on the view by FileView: by path, as
// rows repeat at every level of expanded folders.
constexpr const char* DropTargetProperty = "dropTarget";

bool isDropTarget(const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (!option.widget)
        return false;
    const QString target = option.widget->property(DropTargetProperty).toString();
    return !target.isEmpty() && target == index.data(DirectoryModel::PathRole).toString();
}

// The list view's delegate: shows the full name as a tooltip when it does not fit.
class ListDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
        const QModelIndex& index) override
    {
        if (event->type() != QEvent::ToolTip || index.column() != DirectoryModel::NameColumn)
            return QStyledItemDelegate::helpEvent(event, view, option, index);
        QStyleOptionViewItem item = option;
        initStyleOption(&item, index);
        const QStyle* style = view->style();
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &item, view);
        if (item.fontMetrics.horizontalAdvance(item.text) > textRect.width())
            QToolTip::showText(event->globalPos(), item.text, view);
        else
            QToolTip::hideText();
        return true;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        QStyledItemDelegate::setEditorData(editor, index);
        if (auto* line = qobject_cast<QLineEdit*>(editor)) {
            line->setObjectName(u"InlineEditor"_s);
            selectNameStem(line, index.data(DirectoryModel::IsDirRole).toBool());
        }
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (isDropTarget(option, index)) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(tde::theme::colors().accent, 2));
            // One outline around the whole row: only its first and last cells draw their ends.
            const QRectF rect = QRectF(option.rect).adjusted(1, 1, -1, -1);
            painter->drawLine(rect.topLeft(), rect.topRight());
            painter->drawLine(rect.bottomLeft(), rect.bottomRight());
            if (index.column() == 0)
                painter->drawLine(rect.topLeft(), rect.bottomLeft());
            if (index.column() == index.model()->columnCount() - 1)
                painter->drawLine(rect.topRight(), rect.bottomRight());
            painter->restore();
        }
    }
};

int columnFor(SortKey key)
{
    switch (key) {
    case SortKey::Name:
        return DirectoryModel::NameColumn;
    case SortKey::Size:
        return DirectoryModel::SizeColumn;
    case SortKey::Type:
        return DirectoryModel::TypeColumn;
    case SortKey::Modified:
        return DirectoryModel::ModifiedColumn;
    }
    return DirectoryModel::NameColumn;
}

} // namespace

FileView::FileView(QAbstractItemModel* model, QWidget* parent)
    : QWidget(parent)
    , m_stack(new QStackedWidget(this))
    , m_grid(new GridView(this))
    , m_list(new QTreeView(this))
    , m_placeholder(new QWidget(this))
    , m_placeholderIcon(new QLabel(m_placeholder))
    , m_placeholderText(new QLabel(m_placeholder))
    , m_status(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);

    m_autoScrollTimer.setInterval(30);
    connect(&m_autoScrollTimer, &QTimer::timeout, this, &FileView::autoScroll);

    m_list->setObjectName(u"ListView"_s);
    // Double-clicking a folder opens it, as in the grid; its arrow (or Right) expands it.
    m_list->setExpandsOnDoubleClick(false);
    m_list->setIndentation(20);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setMouseTracking(true);
    m_list->setIconSize(QSize(m_listIconSize, m_listIconSize));
    m_list->setItemDelegate(new ListDelegate(m_list));

    m_grid->setModel(model);
    m_list->setModel(model);
    // Both views share the grid's selection; the list's own one is no longer needed.
    const std::unique_ptr<QItemSelectionModel> ownSelection(m_list->selectionModel());
    m_list->setSelectionModel(m_grid->selectionModel());

    QHeaderView* header = m_list->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(DirectoryModel::NameColumn, QHeaderView::Stretch);
    for (const int column : {DirectoryModel::SizeColumn, DirectoryModel::TypeColumn, DirectoryModel::ModifiedColumn})
        header->setSectionResizeMode(column, QHeaderView::Interactive);
    header->resizeSection(DirectoryModel::SizeColumn, 110);
    header->resizeSection(DirectoryModel::TypeColumn, 150);
    header->resizeSection(DirectoryModel::ModifiedColumn, 130);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    connect(header, &QHeaderView::sectionClicked, this, &FileView::headerClicked);

    setupView(m_grid);
    setupView(m_list);
    m_stack->addWidget(m_grid);
    m_stack->addWidget(m_list);

    m_placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_placeholderText->setObjectName(u"PlaceholderText"_s);
    auto* placeholderLayout = new QVBoxLayout(m_placeholder);
    placeholderLayout->addStretch(1);
    placeholderLayout->addWidget(m_placeholderIcon, 0, Qt::AlignHCenter);
    placeholderLayout->addSpacing(12);
    placeholderLayout->addWidget(m_placeholderText, 0, Qt::AlignHCenter);
    placeholderLayout->addStretch(1);
    m_placeholderText->setWordWrap(true);
    m_placeholderText->setAlignment(Qt::AlignCenter);
    m_placeholder->hide();

    m_status->setObjectName(u"StatusBar"_s);
    m_status->setWordWrap(true);
    m_status->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_status->hide();

    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FileView::selectionChanged);
}

void FileView::setStatusText(const QString& text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
    placeStatus();
}

void FileView::placeStatus()
{
    if (m_status->isHidden())
        return;
    // A small floating bar in the bottom right corner, like Nautilus has.
    const int maximum = std::max(160, width() * 2 / 3);
    m_status->setMaximumWidth(maximum);
    // Wrap only what does not fit on one line; wrapping labels otherwise pick a narrow width.
    m_status->setWordWrap(m_status->fontMetrics().horizontalAdvance(m_status->text()) > maximum - 24);
    m_status->adjustSize();
    m_status->move(width() - m_status->width() - 14, height() - m_status->height() - 12);
    m_status->raise();
}

void FileView::setupView(QAbstractItemView* view)
{
    view->installEventFilter(this);
    view->setAcceptDrops(true);
    view->viewport()->setAcceptDrops(true);
    view->setDropIndicatorShown(false);
    view->setProperty(DropTargetProperty, QString());
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->viewport()->installEventFilter(this);
    connect(view, &QAbstractItemView::activated, this, &FileView::activated);
    connect(view, &QWidget::customContextMenuRequested, this, [this, view](const QPoint& position) {
        const QModelIndex index = view->indexAt(position);
        if (index.isValid() && !view->selectionModel()->isSelected(index))
            selectRow(index);
        else if (!index.isValid())
            view->clearSelection();
        emit contextMenuRequested(view->viewport()->mapToGlobal(position), index);
    });
}

void FileView::setMode(ViewMode mode)
{
    const bool hadFocus = currentView()->hasFocus();
    m_mode = mode;
    m_stack->setCurrentWidget(mode == ViewMode::Grid ? static_cast<QWidget*>(m_grid) : m_list);
    const QModelIndex current = currentView()->currentIndex();
    if (current.isValid())
        currentView()->scrollTo(current);
    if (hadFocus)
        focusView();
}

void FileView::setIconSizes(int grid, int list)
{
    m_grid->setIconSizePx(grid);
    m_listIconSize = list;
    m_list->setIconSize(QSize(list, list));
    emit iconSizesChanged(grid, list);
}

void FileView::setExpandableFolders(bool expandable)
{
    m_list->setRootIsDecorated(expandable);
    m_list->setItemsExpandable(expandable);
}

void FileView::zoom(int steps)
{
    if (m_mode == ViewMode::Grid)
        setIconSizes(stepZoom(GridZoomLevels, m_grid->iconSizePx(), steps), m_listIconSize);
    else
        setIconSizes(m_grid->iconSizePx(), stepZoom(ListZoomLevels, m_listIconSize, steps));
}

QAbstractItemView* FileView::currentView() const
{
    return m_mode == ViewMode::Grid ? static_cast<QAbstractItemView*>(m_grid) : m_list;
}

QModelIndexList FileView::selectedRows() const
{
    // selectedRows() would only count rows with every column selected; the grid shows one.
    QModelIndexList rows;
    for (const QModelIndex& index : m_grid->selectionModel()->selectedIndexes()) {
        const QModelIndex name = index.siblingAtColumn(DirectoryModel::NameColumn);
        if (!rows.contains(name))
            rows << name;
    }
    std::ranges::sort(rows, {}, &QModelIndex::row);
    return rows;
}

void FileView::selectRow(const QModelIndex& index)
{
    QAbstractItemView* view = currentView();
    view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->scrollTo(index);
}

void FileView::focusView()
{
    currentView()->setFocus(Qt::OtherFocusReason);
}

void FileView::setPlaceholder(const QString& iconName, const QString& text)
{
    if (text.isEmpty()) {
        m_placeholder->hide();
        return;
    }

    constexpr int size = 96;
    const qreal scale = devicePixelRatioF();
    QPixmap pixmap(QSize(size, size) * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setOpacity(0.5);
        tde::theme::symbolicIcon(iconName).paint(&painter, QRect(0, 0, size, size));
    }
    m_placeholderIcon->setPixmap(pixmap);
    m_placeholderText->setText(text);
    m_placeholder->setGeometry(rect());
    m_placeholder->show();
    m_placeholder->raise();
}

void FileView::setSortIndicator(SortKey key, bool descending)
{
    m_list->header()->setSortIndicator(columnFor(key), descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void FileView::editName(const QModelIndex& index)
{
    QAbstractItemView* view = currentView();
    selectRow(index);
    const QModelIndex name = index.siblingAtColumn(DirectoryModel::NameColumn);
    view->edit(name);
    // The view selects all of the editor's text when opening it; keep just the name selected,
    // so typing leaves the extension alone.
    const bool isFolder = name.data(DirectoryModel::IsDirRole).toBool();
    QTimer::singleShot(0, view, [view, isFolder] {
        if (auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
            editor && editor->objectName() == u"InlineEditor" && view->isAncestorOf(editor))
            selectNameStem(editor, isFolder);
    });
}

void FileView::setDropDirectory(const QString& path, bool isTrash)
{
    m_dropDirectory = path;
    m_dropIntoTrash = isTrash;
}

namespace {

constexpr int AutoScrollMargin = 40;

} // namespace

void FileView::updateAutoScroll(QAbstractItemView* view, const QPoint& position, const QStringList& paths)
{
    m_autoScrollView = view;
    m_dragPosition = position;
    m_draggedPaths = paths;
    const int height = view->viewport()->height();
    const bool nearEdge = position.y() < AutoScrollMargin || position.y() > height - AutoScrollMargin;
    if (nearEdge && !m_autoScrollTimer.isActive())
        m_autoScrollTimer.start();
    else if (!nearEdge)
        m_autoScrollTimer.stop();
}

void FileView::autoScroll()
{
    QAbstractItemView* view = m_autoScrollView;
    if (!view) {
        stopAutoScroll();
        return;
    }
    // Faster the closer to the edge: up to a few rows a second at the very edge.
    const int height = view->viewport()->height();
    const int y = m_dragPosition.y();
    int step = 0;
    if (y < AutoScrollMargin)
        step = -(AutoScrollMargin - y);
    else if (y > height - AutoScrollMargin)
        step = y - (height - AutoScrollMargin);
    m_autoScrollPixels += step / 2.0 + (step > 0 ? 1 : step < 0 ? -1 : 0);
    // The list scrolls by rows: whole rows once enough pixels have added up.
    int amount = static_cast<int>(m_autoScrollPixels);
    if (view->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
        const int rowHeight = std::max(1, view->sizeHintForRow(0));
        amount = static_cast<int>(m_autoScrollPixels / rowHeight);
        m_autoScrollPixels -= amount * rowHeight;
    } else {
        m_autoScrollPixels -= amount;
    }
    QScrollBar* bar = view->verticalScrollBar();
    const int before = bar->value();
    bar->setValue(before + amount);
    if (bar->value() == before)
        return;
    // The pointer stays put while the files move under it: mark the folder now beneath it.
    const QModelIndex index = view->indexAt(m_dragPosition);
    const QString path = index.data(DirectoryModel::PathRole).toString();
    const bool folder = index.isValid() && index.data(DirectoryModel::IsDirRole).toBool();
    setDropTarget(view, folder && !m_draggedPaths.contains(path) ? path : QString());
}

void FileView::stopAutoScroll()
{
    m_autoScrollTimer.stop();
    m_autoScrollPixels = 0;
    m_autoScrollView = nullptr;
}

void FileView::setDropTarget(QAbstractItemView* view, const QString& path)
{
    if (view->property(DropTargetProperty).toString() == path)
        return;
    view->setProperty(DropTargetProperty, path);
    view->viewport()->update();
}

bool FileView::handleDrag(QAbstractItemView* view, QDropEvent* event)
{
    const QStringList paths = dnd::localPaths(event->mimeData());
    const QPoint position = event->position().toPoint();
    const QModelIndex index = view->indexAt(position);
    if (event->type() == QEvent::Drop)
        stopAutoScroll();
    else
        updateAutoScroll(view, position, paths);

    // Onto a folder that is not itself being dragged, or else onto the folder shown.
    QString directory = m_dropDirectory;
    bool intoTrash = m_dropIntoTrash;
    QString target;
    if (index.isValid() && index.data(DirectoryModel::IsDirRole).toBool()
        && !paths.contains(index.data(DirectoryModel::PathRole).toString())) {
        directory = index.data(DirectoryModel::PathRole).toString();
        intoTrash = false;
        target = directory;
    }

    Qt::DropAction action = Qt::IgnoreAction;
    if (!paths.isEmpty() && intoTrash)
        action = (event->possibleActions() & Qt::MoveAction) ? Qt::MoveAction : Qt::IgnoreAction;
    else if (!paths.isEmpty() && !directory.isEmpty())
        action = dnd::choose(paths, directory, event->modifiers(), event->possibleActions());

    qCDebug(lcDnd) << event->type() << "at" << position << "over"
                   << (index.isValid() ? index.data().toString() : u"-"_s) << "into"
                   << (intoTrash ? u"trash"_s : directory) << "files" << paths.size() << "possible"
                   << event->possibleActions() << "modifiers" << event->modifiers() << "->" << action;
    if (action == Qt::IgnoreAction) {
        setDropTarget(view, {});
        // Entering must be accepted whenever files are dragged, even where they cannot be
        // dropped: a widget that turns the drag away on entering never hears of it again,
        // so the folders further on could not be reached.
        if (event->type() == QEvent::DragEnter && !paths.isEmpty()) {
            event->setDropAction(Qt::IgnoreAction);
            event->accept();
            return true;
        }
        event->ignore();
        return false;
    }
    setDropTarget(view, target);
    event->setDropAction(action);
    event->accept();
    if (event->type() == QEvent::Drop) {
        setDropTarget(view, {});
        emit dropped(paths, intoTrash ? QString() : directory, action);
    }
    return true;
}

bool FileView::eventFilter(QObject* watched, QEvent* event)
{
    // Typing in either view starts a search with what was typed.
    if ((watched == m_grid || watched == m_list) && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const QString text = key->text();
        const bool plain = !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (plain && !text.isEmpty() && text.front().isPrint() && !text.front().isSpace()) {
            emit textTyped(text);
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    const bool isViewport = watched == m_grid->viewport() || watched == m_list->viewport();
    if (!isViewport)
        return QWidget::eventFilter(watched, event);

    auto* view = watched == m_grid->viewport() ? static_cast<QAbstractItemView*>(m_grid) : m_list;
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
        handleDrag(view, static_cast<QDropEvent*>(event));
        return true;
    case QEvent::DragLeave:
        stopAutoScroll();
        setDropTarget(view, {});
        return true;
    default:
        break;
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            const QModelIndex index = view->indexAt(mouse->position().toPoint());
            if (event->type() == QEvent::MouseButtonRelease && index.isValid())
                emit middleClicked(index);
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        const auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            if (wheel->angleDelta().y() != 0)
                zoom(wheel->angleDelta().y() > 0 ? 1 : -1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FileView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_placeholder->setGeometry(rect());
    placeStatus();
}

} // namespace ariadne
