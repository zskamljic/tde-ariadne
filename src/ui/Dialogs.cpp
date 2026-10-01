#include "Dialogs.hpp"

#include "core/Archives.hpp"

#include <tde/Dialog.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace ariadne::dialogs {

std::optional<QString> askPassphrase(QWidget* parent, const QString& volume, const QString& error)
{
    tde::Dialog dialog(u"Unlock Volume"_s, parent);
    auto* heading = new QLabel(u"Enter the passphrase for “%1”"_s.arg(volume));
    heading->setObjectName(u"ConfirmMessage"_s);
    heading->setWordWrap(true);
    heading->setMaximumWidth(460);
    auto* entry = new QLineEdit;
    entry->setEchoMode(QLineEdit::Password);
    entry->setMinimumWidth(340);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* accept = buttons->addButton(u"Unlock"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.contentLayout()->addWidget(heading);
    if (!error.isEmpty()) {
        auto* problem = new QLabel(error);
        problem->setObjectName(u"AboutDetails"_s);
        problem->setWordWrap(true);
        dialog.contentLayout()->addWidget(problem);
    }
    dialog.contentLayout()->addWidget(entry);
    dialog.contentLayout()->addSpacing(6);
    dialog.contentLayout()->addWidget(buttons);
    dialog.setDefaultButton(accept);
    entry->setFocus();
    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    return entry->text();
}

std::optional<QString> answerPrompt(QWidget* parent, const QString& location, const gvfs::Prompt& prompt)
{
    using Kind = gvfs::Prompt::Kind;
    if (prompt.kind == Kind::Choice) {
        tde::Dialog dialog(u"Connect to Server"_s, parent);
        auto* question = new QLabel(prompt.question.isEmpty() ? location : prompt.question);
        question->setWordWrap(true);
        question->setMaximumWidth(460);
        auto* buttons = new QDialogButtonBox;
        std::optional<QString> answer;
        QPushButton* first = nullptr;
        for (qsizetype i = 0; i < prompt.choices.size(); ++i) {
            QPushButton* button = buttons->addButton(prompt.choices[i], QDialogButtonBox::AcceptRole);
            QObject::connect(button, &QPushButton::clicked, &dialog, [&, i] {
                answer = QString::number(i);
                dialog.accept();
            });
            if (!first)
                first = button;
        }
        dialog.contentLayout()->addWidget(question);
        dialog.contentLayout()->addSpacing(6);
        dialog.contentLayout()->addWidget(buttons);
        if (first) {
            dialog.setDefaultButton(first);
            first->setFocus();
        }
        dialog.run();
        return answer;
    }

    const QString label = prompt.kind == Kind::User ? u"User name for %1"_s
        : prompt.kind == Kind::Domain               ? u"Domain for %1"_s
                                                    : u"Password for %1"_s;
    tde::Dialog dialog(u"Connect to Server"_s, parent);
    auto* heading = new QLabel(label.arg(location));
    heading->setWordWrap(true);
    heading->setMaximumWidth(460);
    auto* entry = new QLineEdit(prompt.defaultValue);
    entry->setMinimumWidth(340);
    entry->selectAll();
    if (prompt.kind == Kind::Password)
        entry->setEchoMode(QLineEdit::Password);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* accept = buttons->addButton(u"Connect"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.contentLayout()->addWidget(heading);
    dialog.contentLayout()->addWidget(entry);
    dialog.contentLayout()->addSpacing(6);
    dialog.contentLayout()->addWidget(buttons);
    dialog.setDefaultButton(accept);
    entry->setFocus();
    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    return entry->text();
}

ScriptAction askScriptAction(QWidget* parent, const QString& name)
{
    tde::Dialog dialog(u"Run Script"_s, parent);
    auto* heading = new QLabel(u"Run “%1” or open it?"_s.arg(name));
    heading->setObjectName(u"ConfirmMessage"_s);
    heading->setWordWrap(true);
    heading->setMinimumWidth(340);
    heading->setMaximumWidth(460);
    auto* explanation = new QLabel(u"It is an executable script: it can run as a program, or be opened to see "
                                   u"or change what it does."_s);
    explanation->setObjectName(u"AboutDetails"_s);
    explanation->setWordWrap(true);

    auto* buttons = new QDialogButtonBox;
    ScriptAction action = ScriptAction::Cancel;
    const auto add = [&](const QString& label, ScriptAction chosen) {
        QPushButton* button = buttons->addButton(label, QDialogButtonBox::AcceptRole);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, chosen] {
            action = chosen;
            dialog.accept();
        });
        return button;
    };
    // The button box lays out buttons of one role last to first: Open, Run in Terminal, Run.
    QPushButton* run = add(u"Run"_s, ScriptAction::Run);
    add(u"Run in Terminal"_s, ScriptAction::RunInTerminal);
    add(u"Open"_s, ScriptAction::Open);

    dialog.contentLayout()->addWidget(heading);
    dialog.contentLayout()->addWidget(explanation);
    dialog.contentLayout()->addSpacing(6);
    dialog.contentLayout()->addWidget(buttons);
    dialog.setDefaultButton(run);
    run->setFocus();
    dialog.run();
    return action;
}

ConflictAnswer askConflict(QWidget* parent, const Conflict& conflict)
{
    const QFileInfo existing(conflict.destination);
    const QFileInfo incoming(conflict.source);
    const QString folder
        = existing.absoluteDir().dirName().isEmpty() ? existing.absolutePath() : existing.absoluteDir().dirName();

    tde::Dialog dialog(conflict.folders ? u"Merge Folders"_s : u"Replace File"_s, parent);
    auto* heading = new QLabel(conflict.folders ? u"Merge folder “%1”?"_s.arg(existing.fileName())
                                                : u"Replace “%1”?"_s.arg(existing.fileName()));
    heading->setObjectName(u"ConfirmMessage"_s);
    heading->setWordWrap(true);
    heading->setMinimumWidth(380);
    auto* explanation = new QLabel(conflict.folders
            ? u"A folder with the same name already exists in “%1”. Merging puts the contents of both together, "
              u"asking again for files that exist in both."_s.arg(folder)
            : u"A file with the same name already exists in “%1”. Replacing it overwrites its contents."_s.arg(folder));
    explanation->setObjectName(u"AboutDetails"_s);
    explanation->setWordWrap(true);

    const auto describe = [](const QFileInfo& info) {
        const QString when = QLocale().toString(info.lastModified(), u"d MMM yyyy, HH:mm"_s);
        if (info.isDir())
            return u"Folder, modified %1"_s.arg(when);
        return u"%1, modified %2"_s.arg(QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeSIFormat), when);
    };
    auto* details = new QFormLayout;
    details->addRow(u"Existing"_s, new QLabel(describe(existing)));
    details->addRow(conflict.folders ? u"Merge with"_s : u"Replace with"_s, new QLabel(describe(incoming)));

    auto* applyToAll = new QCheckBox(u"Do the same for all conflicts"_s);

    auto* buttons = new QDialogButtonBox;
    QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton* skip = buttons->addButton(u"Skip"_s, QDialogButtonBox::ActionRole);
    QPushButton* keepBoth = buttons->addButton(u"Keep Both"_s, QDialogButtonBox::ActionRole);
    QPushButton* replace
        = buttons->addButton(conflict.folders ? u"Merge"_s : u"Replace"_s, QDialogButtonBox::ActionRole);
    ConflictChoice choice = ConflictChoice::Cancel;
    const auto choose = [&](ConflictChoice chosen) {
        choice = chosen;
        dialog.accept();
    };
    QObject::connect(skip, &QPushButton::clicked, &dialog, [&] { choose(ConflictChoice::Skip); });
    QObject::connect(keepBoth, &QPushButton::clicked, &dialog, [&] { choose(ConflictChoice::KeepBoth); });
    QObject::connect(replace, &QPushButton::clicked, &dialog,
        [&] { choose(conflict.folders ? ConflictChoice::Merge : ConflictChoice::Replace); });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    QVBoxLayout* layout = dialog.contentLayout();
    layout->addWidget(heading);
    layout->addWidget(explanation);
    layout->addLayout(details);
    layout->addWidget(applyToAll);
    layout->addWidget(buttons);
    dialog.setDefaultButton(skip); // the choice that loses nothing
    skip->setFocus();

    if (dialog.run() != QDialog::Accepted)
        return {ConflictChoice::Cancel, false};
    return {choice, applyToAll->isChecked()};
}

std::optional<QString> askArchiveName(QWidget* parent, const QString& suggestedName, int itemCount)
{
    tde::Dialog dialog(u"Compress"_s, parent);
    auto* prompt = new QLabel(itemCount == 1 ? u"Compress 1 item into an archive named"_s
                                             : u"Compress %1 items into an archive named"_s.arg(itemCount));
    auto* name = new QLineEdit(suggestedName);
    name->setMinimumWidth(260);
    name->selectAll();
    auto* format = new QComboBox;
    for (const QString& extension : archives::compressFormats())
        format->addItem(u"."_s + extension, extension);
    auto* row = new QHBoxLayout;
    row->addWidget(name, 1);
    row->addWidget(format);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* create = buttons->addButton(u"Create"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(name, &QLineEdit::textChanged, &dialog,
        [create](const QString& text) { create->setEnabled(!text.trimmed().isEmpty() && !text.contains(u'/')); });

    dialog.contentLayout()->addWidget(prompt);
    dialog.contentLayout()->addLayout(row);
    dialog.contentLayout()->addWidget(buttons);
    dialog.setDefaultButton(create);
    name->setFocus();
    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    return name->text().trimmed() + u'.' + format->currentData().toString();
}

} // namespace ariadne::dialogs
