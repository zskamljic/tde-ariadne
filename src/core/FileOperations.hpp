#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace ariadne::fileops {

struct Failure {
    QString path;
    QString reason;
};

struct Trashed {
    QString original;
    QString inTrash; // where it went: <trash>/files/<name>
};

struct TrashResult {
    QList<Failure> failures;
    QList<Trashed> trashed;
};

// Moves files to the trash, following the freedesktop.org trash spec: the home trash for
// files on the home file system, the drive's own trash folder for files on other drives.
// Blocking.
TrashResult moveToTrash(const QStringList& paths);

// Puts trashed items back where they were, from any trash folder; for undoing moveToTrash().
QList<Failure> restoreTrashed(const QList<Trashed>& items);

// Deletes files and whole folders for good. Deleting an item from the top of the home
// trash also removes its restore information. Blocking; returns what could not be deleted.
QList<Failure> deletePermanently(const QStringList& paths);

// Where an item at the top of the home trash came from; empty when unknown.
QString originalPath(const QString& trashedPath);

// Moves items from the top of the home trash back to where they came from.
QList<Failure> restoreFromTrash(const QStringList& paths);

// Deletes everything in the home trash.
QList<Failure> emptyTrash();

// `name`, or the first of "name (2)", "name (3)", … (before the extension) not taken in
// `directory`.
QString uniqueName(const QString& directory, const QString& name);

// The name for a copy of `name` in its own folder: "name (Copy)", then "name (Copy 2)", …
QString duplicateName(const QString& directory, const QString& name);

} // namespace ariadne::fileops
