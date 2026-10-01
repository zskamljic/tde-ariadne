#pragma once

#include <QString>
#include <QUrl>

#include <optional>
#include <vector>

// Locations are URLs: file:///path for the local file system and trash:///sub/dir for the
// home trash. All functions return normalized URLs, so they can be compared with ==.
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
bool isLocal(const QUrl& url);

// The directory on disk that holds the contents of `url`.
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
