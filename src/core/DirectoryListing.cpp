#include "DirectoryListing.hpp"

#include "Archives.hpp"
#include "Location.hpp"

#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

int countChildren(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isReadable() || !info.isExecutable())
        return -1;
    int count = 0;
    for ([[maybe_unused]] const auto& entry : QDirListing(path))
        ++count;
    return count;
}

} // namespace

QSet<QString> readHiddenFile(const QString& directory)
{
    QSet<QString> names;
    QFile file(directory + u"/.hidden"_s);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return names;
    while (!file.atEnd()) {
        const QString name = QString::fromUtf8(file.readLine()).trimmed();
        if (!name.isEmpty())
            names.insert(name);
    }
    return names;
}

bool isRemoteFilesystem(const QString& path)
{
    static constexpr QByteArrayView remoteTypes[] = {"nfs", "nfs4", "cifs", "smb3", "smbfs", "9p", "afs", "ceph",
        "glusterfs", "davfs", "fuse.sshfs", "fuse.rclone", "fuse.gvfsd-fuse", "fuse.jmtpfs", "fuse.simple-mtpfs",
        "fuse.aft-mtp-mount", "fuse.curlftpfs"};
    const QByteArray type = QStorageInfo(path).fileSystemType();
    return std::ranges::contains(remoteTypes, QByteArrayView(type));
}

FileEntry describeEntry(
    const QFileInfo& info, const QUrl& url, bool hidden, const QMimeDatabase& mimeDatabase, bool remote)
{
    FileEntry entry;
    entry.name = info.fileName();
    entry.path = info.absoluteFilePath();
    entry.url = url;
    entry.isDir = info.isDir();
    entry.isSymlink = info.isSymLink();
    entry.isHidden = hidden;
    entry.isRemote = remote;
    entry.modified = info.lastModified();
    entry.size = entry.isDir ? 0 : info.size();

    QMimeType mime;
    if (entry.isDir) {
        mime = mimeDatabase.mimeTypeForName(u"inode/directory"_s);
    } else if (!info.exists()) {
        mime = mimeDatabase.mimeTypeForName(u"inode/symlink"_s); // dangling link
    } else {
        mime = mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
        if (mime.isDefault() && info.isFile() && !remote)
            mime = mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchContent);
    }
    entry.mimeType = mime.name();
    entry.mimeComment = mime.comment();
    entry.iconName = mime.iconName();
    entry.genericIconName = mime.genericIconName();

    if (entry.isDir && !remote)
        entry.childCount = countChildren(entry.path);
    return entry;
}

namespace {

// What a folder inside an archive holds. Read-only, and read like a remote folder: nothing
// beyond the listing, which libarchive gives without unpacking anything.
ListingResult listArchive(const QUrl& location)
{
    const auto place = location::archivePlace(location);
    if (!place)
        return std::unexpected(u"The archive is no longer there."_s);
    const auto children = archives::list(place->file, place->inside);
    if (!children)
        return std::unexpected(
            u"“%1” could not be opened: %2"_s.arg(QFileInfo(place->file).fileName(), children.error()));

    const QMimeDatabase mimeDatabase;
    const QString base = location::localPath(location);
    std::vector<FileEntry> entries;
    for (const archives::Entry& child : *children) {
        FileEntry entry;
        entry.name = child.path.section(u'/', -1);
        entry.path = base + u'/' + entry.name;
        entry.url = location::child(location, entry.name);
        entry.isDir = child.isDir;
        entry.isSymlink = child.isSymlink;
        entry.isHidden = entry.name.startsWith(u'.');
        entry.isRemote = true;
        entry.size = child.isDir ? 0 : child.size;
        entry.modified = child.modified;
        const QMimeType mime = child.isDir ? mimeDatabase.mimeTypeForName(u"inode/directory"_s)
                                           : mimeDatabase.mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension);
        entry.mimeType = mime.name();
        entry.mimeComment = mime.comment();
        entry.iconName = mime.iconName();
        entry.genericIconName = mime.genericIconName();
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace

ListingResult listDirectory(const QUrl& location)
{
    if (location::isArchive(location))
        return listArchive(location);
    const QString directory = location::localPath(location);
    const QFileInfo directoryInfo(directory);

    if (!directoryInfo.exists()) {
        // The trash folder is only created once something gets trashed.
        if (location == location::trash())
            return std::vector<FileEntry> {};
        return std::unexpected(u"The folder “%1” does not exist."_s.arg(directory));
    }
    if (!directoryInfo.isDir())
        return std::unexpected(u"“%1” is not a folder."_s.arg(directory));
    if (!directoryInfo.isReadable() || !directoryInfo.isExecutable())
        return std::unexpected(
            u"You do not have permission to view the contents of “%1”."_s.arg(location::displayName(location)));

    const QSet<QString> hiddenNames = readHiddenFile(directory);
    const QMimeDatabase mimeDatabase;
    const bool remote = isRemoteFilesystem(directory);
    std::vector<FileEntry> entries;

    for (const auto& item : QDirListing(directory, QDirListing::IteratorFlag::IncludeHidden)) {
        const QString name = item.fileName();
        const bool hidden = name.startsWith(u'.') || hiddenNames.contains(name);
        entries.push_back(
            describeEntry(item.fileInfo(), location::child(location, name), hidden, mimeDatabase, remote));
    }
    return entries;
}

} // namespace ariadne
