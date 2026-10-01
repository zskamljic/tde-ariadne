#pragma once

#include "core/Gvfs.hpp"
#include "core/Transfer.hpp"

#include <QDialog>

#include <optional>

class QPushButton;
class QVBoxLayout;

namespace ariadne {

// A dialog with the same frameless look as the main windows: a header bar with a centred
// title and the close button where the desktop config puts window buttons.
class Dialog : public QDialog {
    Q_OBJECT

public:
    explicit Dialog(const QString& title, QWidget* parent = nullptr);

    QVBoxLayout* contentLayout() const { return m_content; }

    // Like exec(): shows the dialog and waits until it is closed, keeping other windows from
    // taking input meanwhile, but without asking the compositor for a modal window.
    int run();

    // The button Enter presses. Call it once the button is inside the dialog.
    void setDefaultButton(QPushButton* button);

    // Asks for a line of text; nullopt when cancelled. The first `selectionLength` characters
    // start selected, all of them by default.
    static std::optional<QString> getText(QWidget* parent, const QString& title, const QString& label,
        const QString& text, const QString& acceptLabel, qsizetype selectionLength = -1);

    // Asks before doing something that cannot be undone. Cancel is the default button.
    static bool confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail,
        const QString& acceptLabel, bool destructive = true);

    static void showAbout(QWidget* parent);

    // What to do with an executable script that was opened.
    enum class ScriptAction { Cancel, Open, RunInTerminal, Run };
    static ScriptAction askScriptAction(QWidget* parent, const QString& name);

    // Asks for the passphrase of an encrypted volume; nullopt when cancelled.
    static std::optional<QString> askPassphrase(QWidget* parent, const QString& volume, const QString& error = {});

    // Answers a question asked while mounting `location`; nullopt when cancelled.
    static std::optional<QString> answerPrompt(QWidget* parent, const QString& location, const gvfs::Prompt& prompt);

    // Asks for the name and format of a new archive; returns the file name, extension included.
    static std::optional<QString> askArchiveName(QWidget* parent, const QString& suggestedName, int itemCount);

    // Asks what to do about an item that is already where a copy or move would put another.
    static ConflictAnswer askConflict(QWidget* parent, const Conflict& conflict);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QVBoxLayout* m_content;
};

} // namespace ariadne
