#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSet>
#include <QString>
#include <QUrl>

#include <expected>
#include <vector>

namespace ariadne {

struct FileEntry {
    QString name;
    QString path; // absolute path on disk
    QUrl url; // location of the entry, used to navigate into directories
    QString mimeType;
    QString mimeComment;
    QString iconName;
    QString genericIconName;
    QDateTime modified;
    qint64 size = 0;
    int childCount = -1; // directories only, -1 when unknown
    bool isDir = false;
    bool isSymlink = false;
    bool isHidden = false;
    // On a phone, network share or similar, where reading more than the listing is costly.
    bool isRemote = false;

    bool operator==(const FileEntry&) const = default;
};

using ListingResult = std::expected<std::vector<FileEntry>, QString>;

// Lists the directory behind `location`. Blocking; meant to run on a worker thread.
ListingResult listDirectory(const QUrl& location);

// Names listed in a directory's .hidden file, which count as hidden like dot files.
QSet<QString> readHiddenFile(const QString& directory);

// Whether `path` is on a file system where every read goes over USB or the network: gvfs
// (phones, cameras, network locations), NFS, SMB, sshfs and the like.
bool isRemoteFilesystem(const QString& path);

// Everything shown about one file. Directories also get their item count, which reads them,
// unless the file is `remote`; then only what the listing itself tells is used.
FileEntry describeEntry(
    const QFileInfo& info, const QUrl& url, bool hidden, const QMimeDatabase& mimeDatabase, bool remote = false);

} // namespace ariadne
