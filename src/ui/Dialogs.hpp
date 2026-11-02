#pragma once

#include "core/Gvfs.hpp"
#include "core/Transfer.hpp"

#include <QString>

#include <optional>

class QWidget;

// Ariadne's own questions, in TDE's dialogs.
namespace ariadne::dialogs {

// Asks for the passphrase of an encrypted volume; nullopt when cancelled.
std::optional<QString> askPassphrase(QWidget* parent, const QString& volume, const QString& error = {});

// Answers a question asked while mounting `location`; nullopt when cancelled.
std::optional<QString> answerPrompt(QWidget* parent, const QString& location, const gvfs::Prompt& prompt);

// What to do with an executable script that was opened.
enum class ScriptAction { Cancel, Open, RunInTerminal, Run };
ScriptAction askScriptAction(QWidget* parent, const QString& name);

// Asks for the name and format of a new archive; returns the file name, extension included.
std::optional<QString> askArchiveName(QWidget* parent, const QString& suggestedName, int itemCount);

// Asks what to do about an item that is already where a copy or move would put another.
ConflictAnswer askConflict(QWidget* parent, const Conflict& conflict);

// What to format a drive with: a file system of `types` (UDisks2's names) and a name for it.
struct FormatChoice {
    QString type;
    QString label;
};
// Asks how to format the drive called `name`, warning that what is on it goes.
std::optional<FormatChoice> askFormat(QWidget* parent, const QString& name, const QStringList& types);

} // namespace ariadne::dialogs
