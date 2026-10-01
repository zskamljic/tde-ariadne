#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <expected>
#include <memory>
#include <vector>

// Archives through bsdtar (libarchive): it reads zip, 7z, rar, tar and compressed tarballs,
// ISO images and more, and writes the formats in compressFormats().
namespace ariadne::archives {

bool isAvailable();
bool isArchive(const QString& mimeType);

// The archive's name without its extensions: "photos.tar.gz" → "photos".
QString baseName(const QString& archivePath);

// Extracts into `directory`. An archive holding one item puts it there directly; one
// holding several gets a folder of its own, named after the archive, like Nautilus does.
// Blocking; returns the path of the extracted item or folder.
std::expected<QString, QString> extract(const QString& archivePath, const QString& directory);

// The kinds of archive compress() can make, by extension: "zip", "tar.xz", …
QStringList compressFormats();

// One thing inside an archive.
struct Entry {
    QString path; // inside the archive, like "photos/2024/a.jpg": no leading ./ or /, no trailing /
    QString stored; // the name as stored, which extracting it needs; empty for folders only implied
    bool isDir = false;
    bool isSymlink = false;
    qint64 size = 0;
    QDateTime modified;
};

// What the archive holds, read through libarchive: kept for the last few archives, until
// they change. Blocking.
std::expected<std::shared_ptr<const std::vector<Entry>>, QString> contents(const QString& archivePath);

// What `folder` (a path inside, empty for the top) holds directly, folders included that only
// show up in the paths of what is in them.
std::expected<std::vector<Entry>, QString> list(const QString& archivePath, const QString& folder);

// Extracts `paths` (inside the archive, all in one folder of it) into `directory`, each under
// its own name, folders with everything in them. Blocking.
std::expected<void, QString> extractPaths(
    const QString& archivePath, const QStringList& paths, const QString& directory);

// A new, empty folder for unpacking copies of things in archives, to open or copy them; ones
// older than a day are cleared away.
QString stagingFolder();

// Packs `paths` into a new archive at `archivePath`, whose extension picks the format. The
// items keep their own names inside, wherever they come from. Blocking.
std::expected<void, QString> compress(const QStringList& paths, const QString& archivePath);

} // namespace ariadne::archives
