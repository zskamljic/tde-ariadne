#include "GridView.hpp"

#include "DirectoryModel.hpp"

#include <tde/Theme.hpp>

#include <QAbstractItemView>
#include <QFileInfo>
#include <QHelpEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextLayout>
#include <QToolTip>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr int CellPadding = 4;
constexpr int IconPadding = 6;
constexpr int TextGap = 4;

struct WrappedText {
    QStringList lines;
    bool elided = false;
};

// Wraps `text` into at most `maxLines` lines of `width`, eliding the last one.
WrappedText wrapText(const QString& text, const QFont& font, int width, int maxLines)
{
    const QFontMetrics metrics(font);
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    WrappedText result;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        if (result.lines.size() == maxLines - 1) {
            const QString rest = text.mid(line.textStart());
            const QString elided = metrics.elidedText(rest, Qt::ElideRight, width);
            result.elided = elided != rest;
            result.lines << elided;
            break;
        }
        result.lines << text.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    return result;
}

// Scaled into `box` keeping its shape, with rounded corners and a thin frame, so white
// pages stand out against a light background too.
void paintThumbnail(QPainter* painter, const QRect& box, const QPixmap& pixmap)
{
    const QSizeF size = (QSizeF(pixmap.size()) / pixmap.devicePixelRatio()).scaled(box.size(), Qt::KeepAspectRatio);
    QRectF target(QPointF(), size);
    target.moveCenter(QRectF(box).center());

    const qreal radius = tde::theme::radius(tde::theme::RadiusSize::Small);
    QPainterPath shape;
    shape.addRoundedRect(target, radius, radius);
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setClipPath(shape);
    painter->drawPixmap(target, pixmap, pixmap.rect());
    painter->restore();

    QColor frame = tde::theme::colors().text;
    frame.setAlphaF(0.25f);
    painter->setPen(QPen(frame, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(target.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

} // namespace

GridDelegate::ItemLayout GridDelegate::layout(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    ItemLayout layout;
    layout.cell = option.rect.adjusted(CellPadding, CellPadding, -CellPadding, -CellPadding);
    const int box = m_iconSize + 2 * IconPadding;
    layout.iconArea = QRect(layout.cell.x() + (layout.cell.width() - box) / 2, layout.cell.y(), box, box);

    const QFontMetrics metrics(option.font);
    const WrappedText wrapped
        = wrapText(index.data(Qt::DisplayRole).toString(), option.font, layout.cell.width() - 8, MaxLines);
    layout.lines = wrapped.lines;
    layout.elided = wrapped.elided;
    int widest = 0;
    for (const QString& line : std::as_const(layout.lines))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    layout.textRect = QRect(layout.cell.center().x() - widest / 2 - 4, layout.iconArea.bottom() + TextGap, widest + 8,
        static_cast<int>(layout.lines.size()) * metrics.lineSpacing() + 4);
    return layout;
}

void selectNameStem(QLineEdit* editor, bool isFolder)
{
    const QString name = editor->text();
    qsizetype stem = isFolder ? name.size() : QFileInfo(name).completeBaseName().size();
    if (stem <= 0)
        stem = name.size(); // hidden files like ".bashrc" are all name
    editor->setSelection(0, stem);
}

QWidget* GridDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const
{
    auto* editor = new QLineEdit(parent);
    editor->setObjectName(u"InlineEditor"_s);
    editor->setAlignment(Qt::AlignCenter);
    return editor;
}

void GridDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* line = static_cast<QLineEdit*>(editor);
    line->setText(index.data(Qt::EditRole).toString());
    selectNameStem(line, index.data(DirectoryModel::IsDirRole).toBool());
}

void GridDelegate::updateEditorGeometry(
    QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const ItemLayout item = layout(option, index);
    editor->setGeometry(
        item.cell.left(), item.iconArea.bottom() + TextGap, item.cell.width(), editor->sizeHint().height());
}

bool GridDelegate::helpEvent(
    QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() != QEvent::ToolTip)
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    // Only names that do not fit get a tooltip, showing them in full.
    if (index.isValid() && layout(option, index).elided)
        QToolTip::showText(event->globalPos(), index.data(Qt::DisplayRole).toString(), view);
    else
        QToolTip::hideText();
    return true;
}

bool GridDelegate::hitTest(const QStyleOptionViewItem& option, const QModelIndex& index, const QPoint& point) const
{
    const ItemLayout item = layout(option, index);
    return item.iconArea.contains(point) || item.textRect.contains(point);
}

void GridDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto& colors = tde::theme::colors();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const ItemLayout item = layout(option, index);
    const QRect& cell = item.cell;
    const QRect& iconArea = item.iconArea;
    const QRect& textRect = item.textRect;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (selected || hovered) {
        QColor highlight = selected ? colors.accent : colors.hover;
        if (selected)
            highlight.setAlpha(70);
        QPainterPath path;
        path.addRoundedRect(QRectF(iconArea), tde::theme::radius(tde::theme::RadiusSize::Large),
            tde::theme::radius(tde::theme::RadiusSize::Large));
        painter->fillPath(path, highlight);
    }

    if (option.widget && !option.widget->property("dropTarget").toString().isEmpty()
        && option.widget->property("dropTarget").toString() == index.data(DirectoryModel::PathRole).toString()) {
        painter->setPen(QPen(colors.accent, 2));
        painter->setBrush(Qt::NoBrush);
        const qreal radius = tde::theme::radius(tde::theme::RadiusSize::Large);
        painter->drawRoundedRect(QRectF(iconArea).adjusted(1, 1, -1, -1), radius, radius);
    }

    if (index.data(DirectoryModel::HiddenRole).toBool())
        painter->setOpacity(0.55);
    QRect iconRect(0, 0, m_iconSize, m_iconSize);
    iconRect.moveCenter(iconArea.center());
    if (const QVariant thumbnail = index.data(DirectoryModel::ThumbnailRole); thumbnail.isValid())
        paintThumbnail(painter, iconRect, thumbnail.value<QPixmap>());
    else
        qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).paint(painter, iconRect);

    const QFontMetrics metrics(option.font);
    if (selected) {
        QPainterPath path;
        path.addRoundedRect(QRectF(textRect), tde::theme::radius(), tde::theme::radius());
        painter->setOpacity(1.0);
        painter->fillPath(path, colors.accent);
    }

    painter->setFont(option.font);
    painter->setPen(selected ? colors.accentText : colors.text);
    int y = textRect.top() + 2;
    for (const QString& line : item.lines) {
        painter->drawText(
            QRect(cell.left(), y, cell.width(), metrics.lineSpacing()), Qt::AlignHCenter | Qt::AlignTop, line);
        y += metrics.lineSpacing();
    }
    painter->restore();
}

QSize GridDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    const QFontMetrics metrics(option.font);
    const int width = m_iconSize + 56;
    const int height = 2 * CellPadding + m_iconSize + 2 * IconPadding + TextGap + MaxLines * metrics.lineSpacing() + 8;
    return {width, height};
}

GridView::GridView(QWidget* parent)
    : QListView(parent)
    , m_delegate(new GridDelegate(this))
{
    setObjectName(u"GridView"_s);
    setItemDelegate(m_delegate);
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setUniformItemSizes(true);
    setSpacing(0);
    setLayoutMode(QListView::Batched);
    setBatchSize(500);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionRectVisible(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);
    setIconSizePx(m_iconSize);
}

void GridView::setIconSizePx(int size)
{
    m_iconSize = size;
    m_delegate->setIconSize(size);
    setIconSize(QSize(size, size));
    updateGrid();
}

QModelIndex GridView::indexAt(const QPoint& point) const
{
    // Cells are wider than what they show; only the icon and the name count as the item,
    // so clicks in the gaps between them land on empty space.
    const QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return index;
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.rect = visualRect(index);
    return m_delegate->hitTest(option, index, point) ? index : QModelIndex();
}

void GridView::resizeEvent(QResizeEvent* event)
{
    QListView::resizeEvent(event);
    updateGrid();
}

void GridView::updateGrid()
{
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    const QSize minimum = m_delegate->sizeHint(option, {});

    // QListView always leaves room for the vertical scrollbar when laying out, whether it
    // is shown or not. Using the same width keeps the column count independent of the
    // scrollbar, which would otherwise appear and disappear in a loop near a column
    // boundary. The spare width goes to the cells, whose contents are centred in them.
    const int scrollBarExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
    // One pixel less, because a row wraps unless it is narrower than the available width.
    const int available = std::max(minimum.width(), maximumViewportSize().width() - scrollBarExtent - 1);
    const int columns = std::max(1, available / minimum.width());
    const QSize grid(available / columns, minimum.height());
    if (gridSize() != grid)
        setGridSize(grid);
}

} // namespace ariadne
