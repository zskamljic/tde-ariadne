#pragma once

#include <QString>
#include <QStringList>

#include <expected>

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

// Packs `paths` into a new archive at `archivePath`, whose extension picks the format. The
// items keep their own names inside, wherever they come from. Blocking.
std::expected<void, QString> compress(const QStringList& paths, const QString& archivePath);

} // namespace ariadne::archives
