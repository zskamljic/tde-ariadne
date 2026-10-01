#pragma once

#include "core/Config.hpp"

#include <QModelIndex>
#include <QWidget>

class QAbstractItemModel;
class QAbstractItemView;
class QDropEvent;
class QItemSelectionModel;
class QLabel;
class QStackedWidget;
class QTreeView;

namespace ariadne {

class GridView;

// The main file area: a grid and a list view over the same model and selection, plus a
// placeholder for empty or unreadable folders.
class FileView : public QWidget {
    Q_OBJECT

public:
    explicit FileView(QAbstractItemModel* model, QWidget* parent = nullptr);

    ViewMode mode() const { return m_mode; }
    void setMode(ViewMode mode);
    void setIconSizes(int grid, int list);
    void zoom(int steps);

    QAbstractItemView* currentView() const;
    QModelIndexList selectedRows() const;
    void selectRow(const QModelIndex& index);
    // Starts renaming the item in place.
    void editName(const QModelIndex& index);
    void focusView();

    // Shown instead of the files when `text` is not empty.
    void setPlaceholder(const QString& iconName, const QString& text);
    void setSortIndicator(SortKey key, bool descending);
    // A floating line at the bottom, for what is selected; hidden when empty.
    void setStatusText(const QString& text);
    // Where files dropped onto empty space go: the folder shown, or the trash. An empty path
    // refuses such drops.
    void setDropDirectory(const QString& path, bool isTrash);

signals:
    void activated(const QModelIndex& index);
    void middleClicked(const QModelIndex& index);
    void contextMenuRequested(const QPoint& globalPosition, const QModelIndex& index);
    void headerClicked(int column);
    void iconSizesChanged(int grid, int list);
    void selectionChanged();
    // Printable text was typed while a view had the focus.
    void textTyped(const QString& text);
    // Files were dropped into `directory`; an empty one means into the trash.
    void dropped(const QStringList& paths, const QString& directory, Qt::DropAction action);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupView(QAbstractItemView* view);
    void placeStatus();
    bool handleDrag(QAbstractItemView* view, QDropEvent* event);
    static void setDropRow(QAbstractItemView* view, int row);

    QStackedWidget* m_stack;
    GridView* m_grid;
    QTreeView* m_list;
    QWidget* m_placeholder;
    QLabel* m_placeholderIcon;
    QLabel* m_placeholderText;
    QLabel* m_status;
    ViewMode m_mode = ViewMode::Grid;
    QString m_dropDirectory;
    bool m_dropIntoTrash = false;
    int m_listIconSize = 24;
};

} // namespace ariadne
