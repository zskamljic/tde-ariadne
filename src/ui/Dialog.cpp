#include "Dialog.hpp"

#include "FramelessHelper.hpp"
#include "HeaderBar.hpp"
#include "Theme.hpp"
#include "WindowButtons.hpp"
#include "core/Archives.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

// While a dialog runs, swallows input meant for other windows: the dialog is modal within
// Ariadne, but the compositor sees an ordinary child window, so GNOME Shell does not
// attach and dim it.
class ModalGuard : public QObject {
public:
    explicit ModalGuard(QWidget* dialog)
        : m_dialog(dialog)
    {
        qApp->installEventFilter(this);
    }

    ~ModalGuard() override { qApp->removeEventFilter(this); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::ContextMenu:
        case QEvent::Close:
        case QEvent::DragEnter:
        case QEvent::Drop:
            if (auto* widget = qobject_cast<QWidget*>(watched); widget && !belongsToDialog(widget)) {
                if (event->type() == QEvent::Close)
                    event->ignore();
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    // The dialog itself, or a popup opened from it, like a text field's context menu.
    bool belongsToDialog(const QWidget* widget) const
    {
        for (const QWidget* w = widget; w; w = w->parentWidget()) {
            if (w == m_dialog)
                return true;
        }
        return false;
    }

    QWidget* m_dialog;
};

} // namespace

void Dialog::setDefaultButton(QPushButton* button)
{
    // Enter presses the button with the focus, or this one while no button has it.
    for (QPushButton* other : findChildren<QPushButton*>())
        other->setAutoDefault(true);
    button->setDefault(true);
}

bool Dialog::eventFilter(QObject* watched, QEvent* event)
{
    // Left and right move between the buttons, in the order they are shown.
    if (event->type() == QEvent::KeyPress && qobject_cast<QPushButton*>(watched)) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            QList<QPushButton*> buttons;
            for (QPushButton* button : findChildren<QPushButton*>()) {
                if (button->isVisible() && button->isEnabled() && button->focusPolicy() != Qt::NoFocus)
                    buttons << button;
            }
            std::ranges::sort(buttons, {}, [](const QPushButton* b) { return b->mapToGlobal(QPoint()).x(); });
            const qsizetype at = buttons.indexOf(static_cast<QPushButton*>(watched));
            const qsizetype next = at + (key == Qt::Key_Right ? 1 : -1);
            if (at >= 0 && next >= 0 && next < buttons.size())
                buttons[next]->setFocus(Qt::TabFocusReason);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

int Dialog::run()
{
    setWindowModality(Qt::NonModal);
    ModalGuard guard(this);
    QEventLoop loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    for (QPushButton* button : findChildren<QPushButton*>())
        button->installEventFilter(this);
    show();
    raise();
    activateWindow();
    loop.exec();
    return result();
}

Dialog::Dialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(u"Dialog"_s);
    setAttribute(Qt::WA_StyledBackground);
    setWindowTitle(title);
    new FramelessHelper(this);

    // Dialogs only get a close button, placed where the other windows have theirs.
    const auto& config = tde::desktop().windowButtons;
    std::vector<WindowButton> buttons;
    if (std::ranges::contains(config.order, WindowButton::Close))
        buttons.push_back(WindowButton::Close);

    auto* header = new HeaderBar(this);
    auto* windowButtons = new WindowButtons(buttons, header);
    auto* titleLabel = new QLabel(title, header);
    titleLabel->setObjectName(u"DialogTitle"_s);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // An empty widget as wide as the buttons on the other side keeps the title centred.
    auto* balance = new QWidget(header);
    balance->setFixedWidth(windowButtons->sizeHint().width());
    const bool left = config.side == tde::ButtonSide::Left;
    QHBoxLayout* headerLayout = header->contentLayout();
    headerLayout->setContentsMargins(10, 6, 10, 7);
    headerLayout->addWidget(left ? windowButtons : balance);
    headerLayout->addWidget(titleLabel, 1);
    headerLayout->addWidget(left ? balance : windowButtons);

    auto* body = new QWidget(this);
    body->setObjectName(u"DialogBody"_s);
    body->setAttribute(Qt::WA_StyledBackground);
    m_content = new QVBoxLayout(body);
    m_content->setContentsMargins(20, 18, 20, 18);
    m_content->setSpacing(12);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addWidget(header);
    layout->addWidget(body);
}

std::optional<QString> Dialog::getText(QWidget* parent, const QString& title, const QString& label, const QString& text,
    const QString& acceptLabel, qsizetype selectionLength)
{
    Dialog dialog(title, parent);
    auto* prompt = new QLabel(label);
    auto* entry = new QLineEdit(text);
    entry->setMinimumWidth(320);
    entry->setSelection(0, selectionLength < 0 ? text.size() : selectionLength);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* accept = buttons->addButton(acceptLabel, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.contentLayout()->addWidget(prompt);
    dialog.contentLayout()->addWidget(entry);
    dialog.contentLayout()->addWidget(buttons);
    // Only once the buttons are inside the dialog does it learn which one is the default.
    dialog.setDefaultButton(accept);
    entry->setFocus();
    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    return entry->text();
}

std::optional<QString> Dialog::askPassphrase(QWidget* parent, const QString& volume, const QString& error)
{
    Dialog dialog(u"Unlock Volume"_s, parent);
    auto* heading = new QLabel(u"Enter the passphrase for “%1”"_s.arg(volume));
    heading->setObjectName(u"ConfirmMessage"_s);
    heading->setWordWrap(true);
    heading->setMaximumWidth(460);
    auto* entry = new QLineEdit;
    entry->setEchoMode(QLineEdit::Password);
    entry->setMinimumWidth(340);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* accept = buttons->addButton(u"Unlock"_s, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

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

std::optional<QString> Dialog::answerPrompt(QWidget* parent, const QString& location, const gvfs::Prompt& prompt)
{
    using Kind = gvfs::Prompt::Kind;
    if (prompt.kind == Kind::Choice) {
        Dialog dialog(u"Connect to Server"_s, parent);
        auto* question = new QLabel(prompt.question.isEmpty() ? location : prompt.question);
        question->setWordWrap(true);
        question->setMaximumWidth(460);
        auto* buttons = new QDialogButtonBox;
        std::optional<QString> answer;
        QPushButton* first = nullptr;
        for (qsizetype i = 0; i < prompt.choices.size(); ++i) {
            QPushButton* button = buttons->addButton(prompt.choices[i], QDialogButtonBox::AcceptRole);
            connect(button, &QPushButton::clicked, &dialog, [&, i] {
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
    Dialog dialog(u"Connect to Server"_s, parent);
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
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
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

Dialog::ScriptAction Dialog::askScriptAction(QWidget* parent, const QString& name)
{
    Dialog dialog(u"Run Script"_s, parent);
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
        connect(button, &QPushButton::clicked, &dialog, [&, chosen] {
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

bool Dialog::confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail,
    const QString& acceptLabel, bool destructive)
{
    Dialog dialog(title, parent);
    auto* heading = new QLabel(message);
    heading->setObjectName(u"ConfirmMessage"_s);
    heading->setWordWrap(true);
    heading->setMinimumWidth(340);
    heading->setMaximumWidth(460);
    auto* explanation = new QLabel(detail);
    explanation->setObjectName(u"AboutDetails"_s);
    explanation->setWordWrap(true);

    auto* buttons = new QDialogButtonBox;
    QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton* accept = buttons->addButton(acceptLabel, QDialogButtonBox::AcceptRole);
    if (destructive)
        accept->setObjectName(u"DestructiveButton"_s);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.contentLayout()->addWidget(heading);
    dialog.contentLayout()->addWidget(explanation);
    dialog.contentLayout()->addSpacing(6);
    dialog.contentLayout()->addWidget(buttons);
    QPushButton* focused = destructive ? cancel : accept;
    dialog.setDefaultButton(focused);
    focused->setFocus();
    return dialog.run() == QDialog::Accepted;
}

ConflictAnswer Dialog::askConflict(QWidget* parent, const Conflict& conflict)
{
    const QFileInfo existing(conflict.destination);
    const QFileInfo incoming(conflict.source);
    const QString folder
        = existing.absoluteDir().dirName().isEmpty() ? existing.absolutePath() : existing.absoluteDir().dirName();

    Dialog dialog(conflict.folders ? u"Merge Folders"_s : u"Replace File"_s, parent);
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
    connect(skip, &QPushButton::clicked, &dialog, [&] { choose(ConflictChoice::Skip); });
    connect(keepBoth, &QPushButton::clicked, &dialog, [&] { choose(ConflictChoice::KeepBoth); });
    connect(replace, &QPushButton::clicked, &dialog,
        [&] { choose(conflict.folders ? ConflictChoice::Merge : ConflictChoice::Replace); });
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

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

std::optional<QString> Dialog::askArchiveName(QWidget* parent, const QString& suggestedName, int itemCount)
{
    Dialog dialog(u"Compress"_s, parent);
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
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(name, &QLineEdit::textChanged, &dialog,
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

void Dialog::showAbout(QWidget* parent)
{
    Dialog dialog(u"About Ariadne"_s, parent);

    auto* icon = new QLabel;
    icon->setPixmap(QApplication::windowIcon().pixmap(QSize(96, 96), dialog.devicePixelRatioF()));
    auto* name = new QLabel(u"Ariadne"_s);
    name->setObjectName(u"AboutName"_s);
    auto* version = new QLabel(u"Version %1"_s.arg(QApplication::applicationVersion()));
    version->setObjectName(u"AboutDetails"_s);
    auto* description = new QLabel(u"A file manager, part of TDE."_s);

    QVBoxLayout* layout = dialog.contentLayout();
    layout->setContentsMargins(48, 24, 48, 32);
    layout->setSpacing(6);
    for (QLabel* label : {icon, name, version})
        layout->addWidget(label, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    layout->addWidget(description, 0, Qt::AlignHCenter);
    dialog.run();
}

} // namespace ariadne
