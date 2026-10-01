#pragma once

#include <QLoggingCategory>
#include <QStringList>
#include <Qt>

// Drag and drop decisions, for debugging: QT_LOGGING_RULES="ariadne.dnd.debug=true".
Q_DECLARE_LOGGING_CATEGORY(lcDnd)

class QMimeData;

namespace ariadne::dnd {

// The local files being dragged, if any.
QStringList localPaths(const QMimeData* data);

// What dropping `paths` into `directory` does: a move within one file system and a copy
// across, like other file managers; Ctrl forces a copy, Shift a move. IgnoreAction when the
// drop would be pointless or impossible, such as a folder into itself or files onto the
// folder they are already in.
Qt::DropAction choose(
    const QStringList& paths, const QString& directory, Qt::KeyboardModifiers modifiers, Qt::DropActions possible);

} // namespace ariadne::dnd
