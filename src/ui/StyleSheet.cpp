#include "StyleSheet.hpp"

using namespace Qt::StringLiterals;

namespace ariadne {

QString styleSheet()
{
    return uR"(
QWidget#MainWindow { background: @border@; }
QWidget#SidebarColumn { background: @sidebar@; }
QWidget#ContentColumn { background: @base@; }

QListWidget#Sidebar { background: @sidebar@; color: @sidebar_text@; border: none; }

QFrame#PathBar { background: @entry@; border: 1px solid @border@; border-radius: @radius@; }
QFrame#PathBar[editing="true"] { border-color: @accent@; }
QFrame#PathBar[error="true"] { border-color: @error@; }
QScrollArea#CrumbArea, QWidget#CrumbContainer { background: transparent; border: none; }
QToolButton#Crumb {
    background: transparent; color: @dim_text@;
    border: none; border-radius: @radius_small@; padding: 3px 7px;
}
QToolButton#Crumb:hover { background: @hover@; color: @text@; }
QToolButton#Crumb[current="true"] { color: @text@; font-weight: bold; }
QLabel#CrumbSeparator { color: @dim_text@; }
QLineEdit#PathEntry {
    background: transparent; color: @text@; border: none; padding: 0 8px;
    selection-background-color: @accent@; selection-color: @accent_text@;
}

QListView#GridView, QTreeView#ListView { background: @base@; color: @text@; border: none; }
QTreeView#ListView::item { border: none; padding: 3px 0; }
QTreeView#ListView::item:hover { background: @hover@; }
QTreeView#ListView::item:selected { background: @accent@; color: @accent_text@; }

QLabel#PlaceholderText { color: @dim_text@; font-size: 15pt; font-weight: bold; }
QWidget#InfoBar { background: @window@; border-bottom: 1px solid @border@; }
QLineEdit#InlineEditor {
    background: @entry@; border: 1px solid @accent@; border-radius: @radius_small@; padding: 1px 4px;
}
QLineEdit#SearchEntry { border-radius: @radius@; padding: 6px 8px; }
QLabel#StatusBar {
    background: @header@; color: @text@;
    border: 1px solid @border@; border-radius: @radius@; padding: 5px 10px;
}
)"_s;
}

} // namespace ariadne
