#pragma once

#include <QMimeData>
#include <QStringList>

#include <memory>
#include <optional>

// Files on the clipboard, in the formats other file managers and applications use:
// x-special/gnome-copied-files (GTK file managers), application/x-kde-cutselection (KDE),
// and a plain URI list for everyone else.
namespace ariadne::clipboard {

struct Files {
    QStringList paths;
    bool cut = false; // pasting moves the files instead of copying them

    bool operator==(const Files&) const = default;
};

std::unique_ptr<QMimeData> encode(const Files& files);
// Local files on the clipboard, if there are any.
std::optional<Files> decode(const QMimeData* data);

} // namespace ariadne::clipboard
