#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <expected>
#include <vector>

class QMimeDatabase;

namespace ariadne {

// A command of the user's own in the context menu, from the `actions` list in config.lua.
struct CustomAction {
    // What it works on.
    enum class Selection {
        Any, // one or more items
        Single, // exactly one
        Multiple, // two or more
        None, // nothing selected: the folder shown, from the menu of its empty space
    };

    QString name;
    // The program and its arguments. %f is the first file, %F all of them (each its own
    // argument when written alone), %u and %U the same as URIs, %d the folder shown, %% a %.
    QStringList command;
    QString icon;
    QString shortcut; // like "Ctrl+Alt+T"
    // MIME types the items must all have (or inherit), like "image/png", "image/*" or
    // "inode/directory" for folders. Empty: anything.
    QStringList types;
    Selection selection = Selection::Any;
    bool terminal = false;

    bool operator==(const CustomAction&) const = default;
};

// An item the action is offered for.
struct ActionTarget {
    QString path;
    QString mimeType;
};

// Whether `action` is offered for `targets` (empty: the empty space of a folder).
bool appliesTo(const CustomAction& action, const std::vector<ActionTarget>& targets, const QMimeDatabase& mimes);

// The command line for `targets`, shown in `folder`.
QStringList expandCommand(const CustomAction& action, const std::vector<ActionTarget>& targets, const QString& folder);

// Starts the action in `folder`, in `terminal` (empty: the default one) when it asks for one.
std::expected<void, QString> runAction(const CustomAction& action, const std::vector<ActionTarget>& targets,
    const QString& folder, const QString& terminal = {});

} // namespace ariadne
