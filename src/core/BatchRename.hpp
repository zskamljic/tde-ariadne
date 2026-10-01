#pragma once

#include "FileOperations.hpp"

#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <utility>
#include <vector>

// Renaming many files at once by finding and replacing text in their names.
namespace ariadne::batchrename {

struct Options {
    QString find;
    QString replace;
    bool regex = false; // `find` is a regular expression; `replace` may use \1, \2, …
    bool caseSensitive = false;
    bool includeExtension = false; // also change the extension, not just the name before it
};

struct Plan {
    QString path;
    QString oldName;
    QString newName;
    QString problem; // why it cannot be renamed; empty when it can

    bool changes() const { return newName != oldName; }
};

// What renaming `paths` would give, or why the options are invalid (a broken expression).
std::expected<std::vector<Plan>, QString> plan(const QStringList& paths, const Options& options);

struct Outcome {
    QList<fileops::Failure> failures;
    QList<std::pair<QString, QString>> renamed; // old path → new path
};

// Renames each path to the other in its pair, in an order that works even when names are
// swapped or passed along a chain (a → b while b → c).
Outcome renameAll(const QList<std::pair<QString, QString>>& moves);

// Applies the plans that change a name and have no problem.
Outcome apply(const std::vector<Plan>& plans);

} // namespace ariadne::batchrename
