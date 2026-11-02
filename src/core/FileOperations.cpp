#include "FileOperations.hpp"

#include "Location.hpp"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace ariadne::fileops {
namespace {

QString trashInfoPath(const QString& trashedName)
{
    return QFileInfo(location::trashFilesPath()).absolutePath() + u"/info/"_s + trashedName + u".trashinfo"_s;
}

bool isTopOfTrash(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absolutePath()) == QDir::cleanPath(location::trashFilesPath());
}

} // namespace

TrashResult moveToTrash(const QStringList& paths, const JobControl& control)
{
    // Qt cannot create the home trash when the data folder itself is missing.
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));

    TrashResult result;
    TransferProgress progress {.totalFiles = int(paths.size())};
    for (const QString& path : paths) {
        if (control.cancelled())
            break;
        progress.current = QFileInfo(path).fileName();
        control.report(progress);
        ++progress.doneFiles;
        QFile file(path);
        if (file.moveToTrash())
            result.trashed << Trashed {QDir::cleanPath(QFileInfo(path).absoluteFilePath()), file.fileName()};
        else
            result.failures << Failure {path, file.errorString()};
    }
    return result;
}

QList<Failure> restoreTrashed(const QList<Trashed>& items)
{
    QList<Failure> failures;
    for (const Trashed& item : items) {
        if (const QFileInfo target(item.original); target.exists() || target.isSymLink()) {
            failures << Failure {item.inTrash, u"“%1” already exists."_s.arg(item.original)};
            continue;
        }
        QDir().mkpath(QFileInfo(item.original).absolutePath());
        if (!QDir().rename(item.inTrash, item.original)) {
            failures << Failure {item.inTrash, u"It could not be moved back to “%1”."_s.arg(item.original)};
            continue;
        }
        // <trash>/files/<name> has its restore information in <trash>/info/<name>.trashinfo.
        const QFileInfo trashed(item.inTrash);
        QFile::remove(
            QFileInfo(trashed.absolutePath()).absolutePath() + u"/info/"_s + trashed.fileName() + u".trashinfo"_s);
    }
    return failures;
}

namespace {

// Everything in `path` and `path` itself, folders not followed through links.
int countTree(const QString& path, const JobControl& control)
{
    int count = 1;
    if (const QFileInfo info(path); !info.isDir() || info.isSymLink())
        return count;
    for (const auto& entry : QDirListing(path,
             QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::IncludeHidden
                 | QDirListing::IteratorFlag::IncludeBrokenSymlinks)) {
        Q_UNUSED(entry);
        if (++count % 1000 == 0 && control.cancelled())
            break;
    }
    return count;
}

// Deletes `path` and all that is in it, the contents first, telling of each item.
bool removeTree(const QString& path, TransferProgress& progress, const JobControl& control)
{
    if (control.cancelled())
        return false;
    const QFileInfo info(path);
    bool removed = true;
    if (info.isDir() && !info.isSymLink()) {
        const QDir folder(path);
        for (const QString& name :
            folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (!removeTree(folder.filePath(name), progress, control))
                removed = false;
        }
        removed = removed && QDir().rmdir(path);
    } else {
        removed = QFile::remove(path);
    }
    progress.current = info.fileName();
    ++progress.doneFiles;
    control.report(progress);
    return removed;
}

} // namespace

QList<Failure> deletePermanently(const QStringList& paths, const JobControl& control)
{
    const QString trashFiles = QDir::cleanPath(location::trashFilesPath());
    TransferProgress progress;
    for (const QString& path : paths)
        progress.totalFiles += countTree(path, control);
    control.report(progress);

    QList<Failure> failures;
    for (const QString& path : paths) {
        if (control.cancelled())
            break;
        const QFileInfo info(path);
        if (!info.isDir() || info.isSymLink()) {
            // A file says why it could not go.
            QFile file(path);
            const bool removed = file.remove();
            progress.current = info.fileName();
            ++progress.doneFiles;
            control.report(progress);
            if (!removed) {
                failures << Failure {path, file.errorString()};
                continue;
            }
        } else if (!removeTree(path, progress, control)) {
            if (control.cancelled())
                break;
            failures << Failure {path, u"Some of its contents could not be deleted."_s};
            continue;
        }

        // Items at the top of the trash have a .trashinfo file describing where they came from.
        if (QDir::cleanPath(info.absolutePath()) == trashFiles)
            QFile::remove(trashInfoPath(info.fileName()));
    }
    return failures;
}

QString originalPath(const QString& trashedPath)
{
    if (!isTopOfTrash(trashedPath))
        return {};
    QFile file(trashInfoPath(QFileInfo(trashedPath).fileName()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith("Path=")) {
            // The home trash stores absolute, percent-encoded paths.
            const QString path = QString::fromUtf8(QByteArray::fromPercentEncoding(line.mid(5)));
            return QDir::isAbsolutePath(path) ? QDir::cleanPath(path) : QString();
        }
    }
    return {};
}

QList<Failure> restoreFromTrash(const QStringList& paths)
{
    QList<Failure> failures;
    for (const QString& path : paths) {
        if (!isTopOfTrash(path)) {
            failures << Failure {path, u"Only whole items in the Trash can be restored, not their contents."_s};
            continue;
        }
        const QString original = originalPath(path);
        if (original.isEmpty()) {
            failures << Failure {path, u"Its original location is unknown."_s};
            continue;
        }
        if (const QFileInfo target(original); target.exists() || target.isSymLink()) {
            failures << Failure {path, u"“%1” already exists."_s.arg(original)};
            continue;
        }
        QDir().mkpath(QFileInfo(original).absolutePath());
        if (!QDir().rename(path, original)) {
            failures << Failure {path, u"It could not be moved back to “%1”."_s.arg(original)};
            continue;
        }
        QFile::remove(trashInfoPath(QFileInfo(path).fileName()));
    }
    return failures;
}

QList<Failure> emptyTrash(const JobControl& control)
{
    QStringList paths;
    const QDir files(location::trashFilesPath());
    for (const QString& name : files.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        paths << files.filePath(name);
    QList<Failure> failures = deletePermanently(paths, control);

    // Restore information for items that are gone, and the size cache, go too.
    const QString trash = QFileInfo(location::trashFilesPath()).absolutePath();
    const QDir info(trash + u"/info"_s);
    for (const QString& name : info.entryList({u"*.trashinfo"_s}, QDir::Files | QDir::Hidden)) {
        if (!QFileInfo(files.filePath(name.chopped(10))).exists())
            QFile::remove(info.filePath(name));
    }
    QFile::remove(trash + u"/directorysizes"_s);
    return failures;
}

namespace {

struct NameParts {
    QString stem;
    QString suffix; // with its dot, possibly several parts: ".tar.gz"
};

// Splits a name for numbering, dropping a marker left by earlier numbering, so that
// "report (Copy).txt" continues as "report (Copy 2).txt" rather than growing.
NameParts splitName(const QString& directory, const QString& name)
{
    NameParts parts {name, {}};
    // Folders have no extension, whatever dots their names contain.
    if (!QFileInfo(QDir(directory).filePath(name)).isDir()) {
        QString known = QMimeDatabase().suffixForFileName(name);
        if (known.isEmpty())
            known = QFileInfo(name).suffix();
        if (!known.isEmpty() && name.size() > known.size() + 1) {
            parts.suffix = u'.' + known;
            parts.stem = name.chopped(known.size() + 1);
        }
    }
    static const QRegularExpression marker(u" \\((Copy|Copy \\d+|\\d+)\\)$"_s);
    parts.stem.remove(marker);
    return parts;
}

bool taken(const QDir& directory, const QString& name)
{
    const QFileInfo info(directory.filePath(name));
    return info.exists() || info.isSymLink();
}

} // namespace

QString uniqueName(const QString& directory, const QString& name)
{
    const QDir dir(directory);
    if (!taken(dir, name))
        return name;
    const NameParts parts = splitName(directory, name);
    for (int number = 2;; ++number) {
        const QString candidate = u"%1 (%2)%3"_s.arg(parts.stem).arg(number).arg(parts.suffix);
        if (!taken(dir, candidate))
            return candidate;
    }
}

QString duplicateName(const QString& directory, const QString& name)
{
    const QDir dir(directory);
    const NameParts parts = splitName(directory, name);
    const QString first = u"%1 (Copy)%2"_s.arg(parts.stem, parts.suffix);
    if (!taken(dir, first))
        return first;
    for (int number = 2;; ++number) {
        const QString candidate = u"%1 (Copy %2)%3"_s.arg(parts.stem).arg(number).arg(parts.suffix);
        if (!taken(dir, candidate))
            return candidate;
    }
}

QFile::Permissions modeOf(const QString& path)
{
    // The User flags say what the current user may do; set, Qt takes them for the owner's.
    return QFileInfo(path).permissions() & ~(QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
}

QList<Failure> changeEnclosedPermissions(
    const QString& folder, std::optional<PermissionChange> files, std::optional<PermissionChange> folders)
{
    QList<Failure> failures;
    if (!files && !folders)
        return failures;
    // Everything is found first: taking access to a folder away midway would hide what is in it.
    QStringList filePaths;
    QStringList folderPaths;
    using Flag = QDirListing::IteratorFlag;
    for (const auto& entry : QDirListing(folder, Flag::Recursive | Flag::IncludeHidden)) {
        if (!entry.isSymLink())
            (entry.isDir() ? folderPaths : filePaths) << entry.absoluteFilePath();
    }
    const auto apply = [&](const QString& path, const PermissionChange& change) {
        const QFile::Permissions current = modeOf(path);
        const QFile::Permissions wanted = (current & ~change.mask) | (change.bits & change.mask);
        if (wanted != current && !QFile::setPermissions(path, wanted))
            failures.append({path, u"The permissions could not be changed."_s});
    };
    if (files) {
        for (const QString& path : std::as_const(filePaths))
            apply(path, *files);
    }
    // Deepest first, for the same reason.
    if (folders) {
        for (auto it = folderPaths.crbegin(); it != folderPaths.crend(); ++it)
            apply(*it, *folders);
    }
    return failures;
}

} // namespace ariadne::fileops
