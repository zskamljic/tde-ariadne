#pragma once

#include "DirectoryListing.hpp"

#include <QString>
#include <QUrl>

#include <functional>
#include <stop_token>
#include <vector>

namespace ariadne::search {

// Whether `name` matches `query`: every word of the query appears in the name, ignoring
// case and accents ("resume" finds "Résumé 2024.pdf").
bool matches(const QString& name, const QString& query);

struct Options {
    QString query;
    bool includeHidden = false; // also look at hidden files and inside hidden folders
    int maxResults = 5000;
};

// Searches `root` and everything below it, nearest folders first, handing results to
// `found` in batches. Folder links are not followed. Blocking; returns when done, when
// maxResults is reached or when `stop` is requested.
void run(const QUrl& root, const Options& options, std::stop_token stop,
    const std::function<void(std::vector<FileEntry>)>& found);

} // namespace ariadne::search
