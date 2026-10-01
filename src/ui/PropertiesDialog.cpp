#include "PropertiesDialog.hpp"

#include "core/FileOperations.hpp"
#include "core/Location.hpp"

#include <tde/Dialog.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDirListing>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QStorageInfo>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

struct Usage {
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 folders = 0;
    qint64 available = -1; // on the disk the first item is on; -1 when unknown
    qint64 total = -1;
};

// Adds up everything under `paths`, without following links. Stops early when cancelled.
Usage measure(const QStringList& paths, const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    Usage usage;
    if (const QStorageInfo storage(paths.value(0)); storage.isValid() && storage.isReady()) {
        usage.available = storage.bytesAvailable();
        usage.total = storage.bytesTotal();
    }
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!info.isDir() || info.isSymLink()) {
            usage.bytes += info.size();
            ++usage.files;
            continue;
        }
        ++usage.folders;
        using Flag = QDirListing::IteratorFlag;
        for (const auto& entry : QDirListing(path, Flag::Recursive | Flag::IncludeHidden)) {
            if (*cancelled)
                return usage;
            if (entry.isDir() && !entry.isSymLink()) {
                ++usage.folders;
            } else {
                usage.bytes += entry.size();
                ++usage.files;
            }
        }
    }
    return usage;
}

QString formatSize(qint64 bytes)
{
    const QString size = QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
    return bytes < 1000 ? size : u"%1 (%2 bytes)"_s.arg(size, QLocale().toString(bytes));
}

QString count(qint64 number, const QString& one, const QString& many)
{
    return number == 1 ? u"1 %1"_s.arg(one) : u"%1 %2"_s.arg(QLocale().toString(number), many);
}

QString formatDate(const QDateTime& time)
{
    return time.isValid() ? QLocale().toString(time, u"ddd d MMM yyyy, HH:mm:ss"_s) : QString();
}

QLabel* valueLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setMinimumWidth(240);
    label->setMaximumWidth(360);
    return label;
}

struct AccessLevel {
    QString label;
    QFile::Permissions ofOwner; // shifted for group and others
};

// The permission bits of one class of users: owner, group or others.
enum class Who { Owner, Group, Others };

QFile::Permissions forWho(QFile::Permissions ownerBits, Who who)
{
    const int bits = ownerBits.toInt() >> 12; // ReadOwner … ExeOwner live at 0x7000
    switch (who) {
    case Who::Owner:
        return QFile::Permissions::fromInt(bits << 12);
    case Who::Group:
        return QFile::Permissions::fromInt(bits << 4);
    case Who::Others:
        return QFile::Permissions::fromInt(bits);
    }
    return {};
}

QList<AccessLevel> accessLevels(bool folder)
{
    const auto r = QFile::ReadOwner;
    const auto w = QFile::WriteOwner;
    const auto x = QFile::ExeOwner;
    if (folder)
        return {{u"None"_s, {}}, {u"List files only"_s, r}, {u"Access files"_s, r | x},
            {u"Create and delete files"_s, r | w | x}};
    return {{u"None"_s, {}}, {u"Read-only"_s, r}, {u"Read and write"_s, r | w}};
}

// What `who` may do with all of `paths`, if it is the same for each.
std::optional<QFile::Permissions> commonAccess(const QStringList& paths, QFile::Permissions relevant)
{
    std::optional<QFile::Permissions> common;
    for (const QString& path : paths) {
        const QFile::Permissions bits = fileops::modeOf(path) & relevant;
        if (common && *common != bits)
            return std::nullopt;
        common = bits;
    }
    return common;
}

QString setPermissionsError(int failed)
{
    return failed == 0 ? QString()
        : failed == 1  ? u"The permissions could not be changed."_s
                       : u"The permissions of %1 items could not be changed."_s.arg(failed);
}

// A combo box choosing what `who` may do; changes are applied to the files right away.
QComboBox* accessBox(const QStringList& paths, bool folders, Who who, bool editable, QLabel* error)
{
    auto* box = new QComboBox;
    const QFile::Permissions all = forWho(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner, who);
    // Files keep their executable bit, which has a checkbox of its own.
    const QFile::Permissions relevant = folders ? all : all & ~forWho(QFile::ExeOwner, who);
    const std::optional<QFile::Permissions> current = commonAccess(paths, relevant);

    int selected = -1;
    for (const AccessLevel& level : accessLevels(folders)) {
        const QFile::Permissions bits = forWho(level.ofOwner, who);
        box->addItem(level.label, bits.toInt());
        if (current && bits == *current)
            selected = box->count() - 1;
    }
    if (selected < 0) {
        // Not one of the levels, or not the same for all: shown, but not offered.
        box->addItem(current ? u"Custom"_s : u"Mixed"_s, current ? current->toInt() : -1);
        selected = box->count() - 1;
    }
    box->setCurrentIndex(selected);
    box->setEnabled(editable);

    QObject::connect(box, &QComboBox::activated, box, [box, paths, relevant, error] {
        if (box->currentData().toInt() < 0)
            return;
        const QFile::Permissions wanted = QFile::Permissions::fromInt(box->currentData().toInt());
        int failed = 0;
        for (const QString& path : paths) {
            if (!QFile::setPermissions(path, (fileops::modeOf(path) & ~relevant) | wanted))
                ++failed;
        }
        error->setVisible(failed > 0);
        error->setText(setPermissionsError(failed));
    });
    return box;
}

// Asks how to change the permissions of everything in `folder`, and changes them in the
// background.
void changeEnclosed(QWidget* parent, const QString& folder, QLabel* error)
{
    tde::Dialog dialog(u"Change Permissions for Enclosed Files"_s, parent);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);

    struct Choice {
        QComboBox* box;
        Who who;
        bool folders;
    };
    std::vector<Choice> choices;
    for (const bool folders : {false, true}) {
        auto* heading = new QLabel(folders ? u"Folders"_s : u"Files"_s);
        heading->setObjectName(u"ConfirmMessage"_s);
        form->addRow(heading);
        for (const auto& [label, who] : {std::pair {u"Owner"_s, Who::Owner}, std::pair {u"Group"_s, Who::Group},
                 std::pair {u"Others"_s, Who::Others}}) {
            auto* box = new QComboBox;
            box->addItem(u"Don't change"_s, -1);
            for (const AccessLevel& level : accessLevels(folders))
                box->addItem(level.label, forWho(level.ofOwner, who).toInt());
            form->addRow(label, box);
            choices.push_back({box, who, folders});
        }
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* change = buttons->addButton(u"Change"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.contentLayout()->addLayout(form);
    dialog.contentLayout()->addSpacing(6);
    dialog.contentLayout()->addWidget(buttons);
    dialog.setDefaultButton(change);
    if (dialog.run() != QDialog::Accepted)
        return;

    std::optional<fileops::PermissionChange> files;
    std::optional<fileops::PermissionChange> folders;
    for (const Choice& choice : choices) {
        const int bits = choice.box->currentData().toInt();
        if (bits < 0)
            continue;
        // Files keep their executable bits.
        const QFile::Permissions mask = forWho(choice.folders ? QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                              : QFile::ReadOwner | QFile::WriteOwner,
            choice.who);
        auto& change = choice.folders ? folders : files;
        if (!change)
            change = fileops::PermissionChange {};
        change->mask |= mask;
        change->bits |= QFile::Permissions::fromInt(bits);
    }
    if (!files && !folders)
        return;
    auto* watcher = new QFutureWatcher<QList<fileops::Failure>>(parent);
    QObject::connect(watcher, &QFutureWatcherBase::finished, error, [watcher, error] {
        watcher->deleteLater();
        const int failed = int(watcher->result().size());
        error->setVisible(failed > 0);
        error->setText(setPermissionsError(failed));
    });
    watcher->setFuture(QtConcurrent::run(fileops::changeEnclosedPermissions, folder, files, folders));
}

} // namespace

void showProperties(QWidget* parent, const std::vector<FileEntry>& entries, const QIcon& icon)
{
    if (entries.empty())
        return;
    const bool single = entries.size() == 1;
    const FileEntry& first = entries.front();
    const QFileInfo info(first.path);

    auto* dialog = new tde::Dialog(single ? u"%1 Properties"_s.arg(first.name) : u"Properties"_s, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QVBoxLayout* layout = dialog->contentLayout();

    auto* iconLabel = new QLabel;
    iconLabel->setPixmap(icon.pixmap(QSize(64, 64), dialog->devicePixelRatioF()));
    auto* nameLabel = new QLabel(single ? first.name : u"%1 items"_s.arg(entries.size()));
    nameLabel->setObjectName(u"ConfirmMessage"_s);
    nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nameLabel->setWordWrap(true);
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setMaximumWidth(420);
    layout->addWidget(iconLabel, 0, Qt::AlignHCenter);
    layout->addWidget(nameLabel, 0, Qt::AlignHCenter);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    layout->addLayout(form);

    if (single) {
        form->addRow(u"Type"_s, valueLabel(u"%1 (%2)"_s.arg(first.mimeComment, first.mimeType)));
        if (first.isSymlink)
            form->addRow(u"Link target"_s, valueLabel(info.symLinkTarget()));
    }

    QStringList paths;
    qint64 selectedFolders = 0;
    for (const FileEntry& entry : entries) {
        paths << entry.path;
        selectedFolders += entry.isDir && !entry.isSymlink ? 1 : 0;
    }
    const bool anyFolder = selectedFolders > 0;
    auto* size = valueLabel(anyFolder ? u"Calculating…"_s : QString());
    form->addRow(u"Size"_s, size);
    QLabel* contents = nullptr;
    if (anyFolder) {
        contents = valueLabel(u"Calculating…"_s);
        form->addRow(u"Contents"_s, contents);
    }

    const QString parentPath = info.absolutePath();
    form->addRow(u"Location"_s, valueLabel(location::isTrash(first.url) ? u"Trash"_s : parentPath));
    if (single) {
        form->addRow(u"Modified"_s, valueLabel(formatDate(info.lastModified())));
        form->addRow(u"Accessed"_s, valueLabel(formatDate(info.lastRead())));
        if (const QDateTime created = info.birthTime(); created.isValid())
            form->addRow(u"Created"_s, valueLabel(formatDate(created)));
    }
    // For a folder, how much room is left where it is.
    QLabel* freeSpace = nullptr;
    if (single && first.isDir && location::isLocal(first.url) && !location::isTrash(first.url)) {
        freeSpace = valueLabel(u"Calculating…"_s);
        form->addRow(u"Free space"_s, freeSpace);
    }

    // Permissions, changeable by their owner; for several items at once when they are all
    // files or all folders, as the two have different kinds of access.
    const bool allFolders = std::ranges::all_of(entries, [](const FileEntry& e) { return e.isDir; });
    const bool allFiles = std::ranges::none_of(entries, [](const FileEntry& e) { return e.isDir; });
    const bool links = std::ranges::any_of(entries, [](const FileEntry& e) { return e.isSymlink; });
    if ((allFolders || allFiles) && !links && !location::isTrash(first.url) && location::isLocal(first.url)) {
        const bool owner = std::ranges::all_of(
            entries, [](const FileEntry& e) { return QFileInfo(e.path).ownerId() == ::getuid(); });
        const auto common = [&](auto name) {
            const QString value = name(QFileInfo(first.path));
            const bool same
                = std::ranges::all_of(entries, [&](const FileEntry& e) { return name(QFileInfo(e.path)) == value; });
            return same ? value : u"Various"_s;
        };
        auto* error = new QLabel;
        error->setObjectName(u"AboutDetails"_s);
        error->hide();
        auto* permissions = new QFormLayout;
        permissions->setLabelAlignment(Qt::AlignRight);
        permissions->setHorizontalSpacing(14);
        permissions->setVerticalSpacing(8);
        auto* heading = new QLabel(u"Permissions"_s);
        heading->setObjectName(u"ConfirmMessage"_s);
        layout->addSpacing(6);
        layout->addWidget(heading);
        layout->addLayout(permissions);
        permissions->addRow(u"Owner"_s, valueLabel(common([](const QFileInfo& i) { return i.owner(); })));
        permissions->addRow(u"Access"_s, accessBox(paths, allFolders, Who::Owner, owner, error));
        permissions->addRow(u"Group"_s, valueLabel(common([](const QFileInfo& i) { return i.group(); })));
        permissions->addRow(u"Access"_s, accessBox(paths, allFolders, Who::Group, owner, error));
        permissions->addRow(u"Others"_s, accessBox(paths, allFolders, Who::Others, owner, error));
        if (allFiles) {
            auto* executable = new QCheckBox(u"Allow executing file as program"_s);
            const auto executables = std::ranges::count_if(
                paths, [](const QString& path) { return bool(fileops::modeOf(path) & QFile::ExeOwner); });
            executable->setTristate(executables != 0 && executables != paths.size());
            executable->setCheckState(executables == 0 ? Qt::Unchecked
                    : executables == paths.size()      ? Qt::Checked
                                                       : Qt::PartiallyChecked);
            executable->setEnabled(owner);
            QObject::connect(executable, &QCheckBox::clicked, executable, [executable, paths, error] {
                // Clicked out of "some", it means all; it does not go back to "some".
                executable->setTristate(false);
                const bool on = executable->checkState() != Qt::Unchecked;
                int failed = 0;
                for (const QString& path : paths) {
                    // Execute goes with read, for everyone who may read the file.
                    QFile::Permissions permissions = fileops::modeOf(path);
                    const std::pair<QFile::Permission, QFile::Permission> pairs[] = {
                        {QFile::ReadOwner, QFile::ExeOwner},
                        {QFile::ReadGroup, QFile::ExeGroup},
                        {QFile::ReadOther, QFile::ExeOther},
                    };
                    for (const auto& [read, exe] : pairs)
                        permissions.setFlag(exe, on && permissions.testFlag(read));
                    if (!QFile::setPermissions(path, permissions))
                        ++failed;
                }
                error->setVisible(failed > 0);
                error->setText(setPermissionsError(failed));
            });
            permissions->addRow(QString(), executable);
        }
        if (single && allFolders && owner) {
            auto* enclosed = new QPushButton(u"Change Permissions for Enclosed Files…"_s);
            enclosed->setAutoDefault(false); // Enter is not meant for it
            QObject::connect(enclosed, &QPushButton::clicked, dialog,
                [dialog, path = first.path, error] { changeEnclosed(dialog, path, error); });
            permissions->addRow(QString(), enclosed);
        }
        layout->addWidget(error);
    }

    // Folder sizes can take a while; count them in the background.
    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(dialog, &QObject::destroyed, [cancelled] { *cancelled = true; });
    auto* watcher = new QFutureWatcher<Usage>(dialog);
    QObject::connect(
        watcher, &QFutureWatcherBase::finished, dialog, [watcher, size, contents, freeSpace, selectedFolders] {
            const Usage usage = watcher->result();
            if (freeSpace) {
                const QLocale locale;
                freeSpace->setText(usage.total <= 0
                        ? u"Unknown"_s
                        : u"%1 free of %2"_s.arg(
                              locale.formattedDataSize(usage.available, 1, QLocale::DataSizeSIFormat),
                              locale.formattedDataSize(usage.total, 1, QLocale::DataSizeSIFormat)));
            }
            size->setText(formatSize(usage.bytes));
            if (contents) {
                QStringList parts {count(usage.files, u"file"_s, u"files"_s)};
                if (const qint64 folders = usage.folders - selectedFolders; folders > 0)
                    parts << count(folders, u"folder"_s, u"folders"_s);
                contents->setText(parts.join(u", "_s));
            }
        });
    watcher->setFuture(QtConcurrent::run(measure, paths, cancelled));

    dialog->show();
}

} // namespace ariadne
