#include "DragDrop.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

#include <sys/stat.h>

Q_LOGGING_CATEGORY(lcDnd, "ariadne.dnd", QtWarningMsg)

namespace ariadne::dnd {
namespace {

// The device a path lives on, following symbolic links, or -1.
dev_t deviceOf(const QString& path)
{
    struct stat info {};
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? info.st_dev : dev_t(-1);
}

} // namespace

QStringList localPaths(const QMimeData* data)
{
    QStringList paths;
    if (!data)
        return paths;
    for (const QUrl& url : data->urls()) {
        if (url.isLocalFile())
            paths << QDir::cleanPath(url.toLocalFile());
    }
    return paths;
}

Qt::DropAction choose(
    const QStringList& paths, const QString& directory, Qt::KeyboardModifiers modifiers, Qt::DropActions possible)
{
    if (paths.isEmpty() || !QFileInfo(directory).isDir())
        return Qt::IgnoreAction;

    const QString target = QDir::cleanPath(directory);
    bool allAlreadyThere = true;
    bool sameDevice = true;
    const dev_t targetDevice = deviceOf(target);
    for (const QString& path : paths) {
        if (target == path || target.startsWith(path + u'/'))
            return Qt::IgnoreAction; // into itself
        allAlreadyThere = allAlreadyThere && QDir::cleanPath(QFileInfo(path).absolutePath()) == target;
        sameDevice = sameDevice && deviceOf(path) == targetDevice;
    }

    Qt::DropAction action = sameDevice ? Qt::MoveAction : Qt::CopyAction;
    if (modifiers & Qt::ControlModifier)
        action = Qt::CopyAction;
    else if (modifiers & Qt::ShiftModifier)
        action = Qt::MoveAction;

    // Moving files onto the folder they are in does nothing; copying them duplicates them.
    if (allAlreadyThere && action == Qt::MoveAction)
        return Qt::IgnoreAction;
    if (!(possible & action))
        action = (possible & Qt::CopyAction) ? Qt::CopyAction : Qt::IgnoreAction;
    return action;
}

} // namespace ariadne::dnd
