#include "Transfer.hpp"

#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr qint64 ChunkSize = 1024 * 1024;

bool exists(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

bool isRealFolder(const QFileInfo& info)
{
    return info.isDir() && !info.isSymLink();
}

bool removeAll(const QString& path)
{
    return fileops::deletePermanently({path}).isEmpty();
}

QString uniqueDestination(const QString& destination)
{
    const QFileInfo info(destination);
    return QDir(info.absolutePath()).filePath(fileops::uniqueName(info.absolutePath(), info.fileName()));
}

QString duplicateDestination(const QString& destination)
{
    const QFileInfo info(destination);
    return QDir(info.absolutePath()).filePath(fileops::duplicateName(info.absolutePath(), info.fileName()));
}

} // namespace

Transfer::Transfer(TransferKind kind, QList<TransferItem> items)
    : m_kind(kind)
    , m_items(std::move(items))
{
}

QList<TransferItem> Transfer::into(const QStringList& sources, const QString& directory)
{
    QList<TransferItem> items;
    const QDir target(directory);
    for (const QString& source : sources)
        items << TransferItem {QDir::cleanPath(source), QDir::cleanPath(target.filePath(QFileInfo(source).fileName()))};
    return items;
}

TransferResult Transfer::run()
{
    std::vector<Size> sizes;
    for (const TransferItem& item : std::as_const(m_items)) {
        sizes.push_back(measure(item.source));
        m_progress.totalBytes += sizes.back().bytes;
        m_progress.totalFiles += sizes.back().files;
    }
    m_sinceReport.start();
    report(true);

    for (qsizetype i = 0; i < m_items.size() && !m_cancelled; ++i)
        transfer(m_items[i], sizes[static_cast<std::size_t>(i)]);

    m_result.cancelled = m_cancelled;
    report(true);
    return m_result;
}

Transfer::Size Transfer::measure(const QString& path) const
{
    const QFileInfo info(path);
    if (!isRealFolder(info))
        return {info.isSymLink() ? 0 : info.size(), 1};

    Size size;
    using Flag = QDirListing::IteratorFlag;
    for (const auto& entry : QDirListing(path, Flag::Recursive | Flag::IncludeHidden)) {
        if (m_cancelled)
            break;
        if (!entry.isDir() || entry.isSymLink()) {
            size.bytes += entry.isSymLink() ? 0 : entry.size();
            ++size.files;
        }
    }
    return size;
}

void Transfer::transfer(const TransferItem& item, const Size& size)
{
    const QFileInfo source(item.source);
    if (!source.exists() && !source.isSymLink()) {
        fail(item.source, u"It no longer exists."_s);
        skip(size);
        return;
    }
    const QString sourcePath = QDir::cleanPath(source.absoluteFilePath());
    QString destination = QDir::cleanPath(item.destination);
    m_progress.current = source.fileName();

    if (destination == sourcePath) {
        // Moving something onto itself does nothing; copying it makes a duplicate.
        if (m_kind == TransferKind::Move) {
            skip(size);
            return;
        }
        destination = duplicateDestination(destination);
    }
    if (isRealFolder(source) && destination.startsWith(sourcePath + u'/')) {
        fail(item.source, u"A folder cannot be put inside itself."_s);
        skip(size);
        return;
    }

    bool merge = false;
    if (exists(destination)) {
        const bool folders = isRealFolder(source) && isRealFolder(QFileInfo(destination));
        const auto answer = resolve({sourcePath, destination, folders});
        if (!answer || answer->choice == ConflictChoice::Skip) {
            skip(size);
            return;
        }
        if (answer->choice == ConflictChoice::KeepBoth) {
            destination = uniqueDestination(destination);
        } else if (answer->choice == ConflictChoice::Merge && folders) {
            merge = true;
        } else if (sourcePath.startsWith(destination + u'/')) {
            // Replacing a folder that holds the item itself would delete the item too.
            fail(item.source, u"It cannot replace a folder it is inside of."_s);
            skip(size);
            return;
        } else if (!removeAll(destination)) {
            fail(destination, u"It could not be replaced."_s);
            skip(size);
            return;
        }
    }

    // Within one file system a move is just a rename.
    if (m_kind == TransferKind::Move && !merge && QDir().rename(sourcePath, destination)) {
        m_progress.doneBytes += size.bytes;
        m_progress.doneFiles += size.files;
        m_result.completed << TransferItem {sourcePath, destination};
        report();
        return;
    }

    const bool copied = copyEntry(sourcePath, destination);
    if (m_cancelled) {
        // Leave nothing half done behind, unless it was merged into something that existed.
        if (!merge)
            removeAll(destination);
        return;
    }
    if (copied) {
        if (m_kind == TransferKind::Move && !removeAll(sourcePath))
            fail(sourcePath, u"It was copied, but the original could not be removed."_s);
        m_result.completed << TransferItem {sourcePath, destination};
    } else if (m_kind == TransferKind::Copy || merge) {
        // Partial copies still count, so undo can remove them.
        if (exists(destination))
            m_result.completed << TransferItem {sourcePath, destination};
    }
}

bool Transfer::copyEntry(const QString& source, const QString& destination)
{
    if (m_cancelled)
        return false;
    const QFileInfo info(source);

    if (info.isSymLink()) {
        m_progress.current = info.fileName();
        if (!QFile::link(info.readSymLink(), destination)) {
            fail(source, u"The link could not be created."_s);
            return false;
        }
        ++m_progress.doneFiles;
        report();
        return true;
    }

    if (info.isDir()) {
        if (!QFileInfo(destination).isDir() && !QDir().mkdir(destination)) {
            fail(source, u"The folder “%1” could not be created."_s.arg(destination));
            return false;
        }
        bool ok = true;
        const QDir directory(source);
        const QDir target(destination);
        for (const QString& name :
            directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            if (m_cancelled)
                return false;
            const QString childSource = directory.filePath(name);
            QString childDestination = target.filePath(name);
            if (exists(childDestination)) {
                // Only happens when merging into an existing folder.
                const QFileInfo childInfo(childSource);
                const bool folders = isRealFolder(childInfo) && isRealFolder(QFileInfo(childDestination));
                const auto answer = resolve({childSource, childDestination, folders});
                if (!answer || answer->choice == ConflictChoice::Skip) {
                    skip(measure(childSource));
                    continue;
                }
                if (answer->choice == ConflictChoice::KeepBoth) {
                    childDestination = uniqueDestination(childDestination);
                } else if (!(answer->choice == ConflictChoice::Merge && folders) && !removeAll(childDestination)) {
                    fail(childDestination, u"It could not be replaced."_s);
                    ok = false;
                    continue;
                }
            }
            ok = copyEntry(childSource, childDestination) && ok;
        }
        QFile::setPermissions(destination, info.permissions());
        return ok;
    }

    if (!info.isFile()) {
        fail(source, u"Special files like devices and sockets cannot be copied."_s);
        return false;
    }
    return copyFile(source, destination);
}

bool Transfer::copyFile(const QString& source, const QString& destination)
{
    const QFileInfo info(source);
    m_progress.current = info.fileName();

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        fail(source, in.errorString());
        return false;
    }
    QFile out(destination);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        fail(source, out.errorString());
        return false;
    }

    QByteArray buffer(ChunkSize, Qt::Uninitialized);
    while (true) {
        if (m_cancelled) {
            out.remove();
            return false;
        }
        const qint64 read = in.read(buffer.data(), ChunkSize);
        if (read < 0) {
            fail(source, in.errorString());
            out.remove();
            return false;
        }
        if (read == 0)
            break;
        if (out.write(buffer.constData(), read) != read) {
            fail(source, out.errorString());
            out.remove();
            return false;
        }
        m_progress.doneBytes += read;
        report();
    }

    // Flush first: writing out the last buffered bytes would reset the time again.
    out.flush();
    out.setPermissions(in.permissions());
    out.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
    ++m_progress.doneFiles;
    report();
    return true;
}

std::optional<ConflictAnswer> Transfer::resolve(const Conflict& conflict)
{
    std::optional<ConflictAnswer>& remembered = conflict.folders ? m_forAllFolders : m_forAllFiles;
    if (remembered)
        return remembered;

    report(true);
    const ConflictAnswer answer = m_onConflict ? m_onConflict(conflict) : ConflictAnswer {};
    if (answer.choice == ConflictChoice::Cancel) {
        cancel();
        return std::nullopt;
    }
    if (answer.applyToAll)
        remembered = answer;
    return answer;
}

void Transfer::skip(const Size& size)
{
    m_progress.doneBytes += size.bytes;
    m_progress.doneFiles += size.files;
    report();
}

void Transfer::fail(const QString& path, const QString& reason)
{
    m_result.failures << fileops::Failure {path, reason};
}

void Transfer::report(bool force)
{
    if (!m_onProgress || (!force && m_sinceReport.elapsed() < m_reportIntervalMs))
        return;
    m_sinceReport.restart();
    m_onProgress(m_progress);
}

} // namespace ariadne
