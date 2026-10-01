#pragma once

#include <QString>
#include <QUrl>

#include <optional>
#include <vector>

// Locations are URLs: file:///path for the local file system, trash:///sub/dir for the home
// trash, and archive:///path/to/photos.zip/sub/dir for what is inside an archive (the path
// goes on through the archive file). All functions return normalized URLs, so they can be
// compared with ==.
namespace ariadne::location {

struct Crumb {
    QString label; // empty for the file system root, which is shown as an icon only
    QString iconName; // empty for plain folders
    QUrl url;
};

QUrl home();
QUrl trash();
QUrl root();
QUrl fromLocalPath(const QString& path);

bool isTrash(const QUrl& url);
bool isArchive(const QUrl& url);
// The top of the archive at `archivePath`, shown as a folder.
QUrl archiveRoot(const QString& archivePath);
// Where an archive location is: the archive file, and the folder inside it ("" for the top).
struct ArchivePlace {
    QString file;
    QString inside;
};
std::optional<ArchivePlace> archivePlace(const QUrl& url);
bool isLocal(const QUrl& url);

// The directory on disk that holds the contents of `url`. Inside an archive, the path its
// contents would have, which is not on disk.
QString localPath(const QUrl& url);
QString trashFilesPath();

QUrl child(const QUrl& directory, const QString& name);
std::optional<QUrl> parent(const QUrl& url);
bool isAncestorOf(const QUrl& ancestor, const QUrl& url);

QString displayName(const QUrl& url);

// Text shown when the path bar is being edited, and the reverse conversion. Relative input
// is resolved against `current`.
QString editableText(const QUrl& url);
std::optional<QUrl> fromUserInput(const QString& text, const QUrl& current);

std::vector<Crumb> crumbs(const QUrl& url);

// Icon for well-known folders (home, Documents, Downloads, ...), or an empty string.
QString directoryIconName(const QString& path);

} // namespace ariadne::location
