#pragma once

#include <QListView>
#include <QStyledItemDelegate>

class QLineEdit;

namespace ariadne {

// Selects the part of a name before its extension (all of a folder's name), so typing
// replaces the name but keeps the type.
void selectNameStem(QLineEdit* editor, bool isFolder);

// Icon above a name of up to three lines, with Nautilus-style rounded highlights.
class GridDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setIconSize(int size) { m_iconSize = size; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
        const QModelIndex& index) override;

    // Renaming in place: an entry field where the name is.
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void updateEditorGeometry(
        QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Whether `point` is on the icon or the name, rather than the empty rest of the cell.
    bool hitTest(const QStyleOptionViewItem& option, const QModelIndex& index, const QPoint& point) const;

    static constexpr int MaxLines = 3;

private:
    struct ItemLayout {
        QRect cell;
        QRect iconArea;
        QRect textRect;
        QStringList lines;
        bool elided = false;
    };
    ItemLayout layout(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    int m_iconSize = 64;
};

// Icon grid whose columns stretch to use the full width, like Nautilus.
class GridView : public QListView {
    Q_OBJECT

public:
    explicit GridView(QWidget* parent = nullptr);

    int iconSizePx() const { return m_iconSize; }
    void setIconSizePx(int size);

    QModelIndex indexAt(const QPoint& point) const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateGrid();

    GridDelegate* m_delegate;
    int m_iconSize = 64;
};

} // namespace ariadne
