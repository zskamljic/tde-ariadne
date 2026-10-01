#include "PropertiesDialog.hpp"

#include "Dialog.hpp"
#include "core/Location.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDirListing>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <atomic>
#include <memory>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

struct Usage {
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 folders = 0;
};

// Adds up everything under `paths`, without following links. Stops early when cancelled.
Usage measure(const QStringList& paths, const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    Usage usage;
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

// A combo box choosing what `who` may do; changes are applied to the file right away.
QComboBox* accessBox(const QString& path, bool folder, Who who, bool editable, QLabel* error)
{
    auto* box = new QComboBox;
    const QFile::Permissions all = forWho(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner, who);
    // Files keep their executable bit, which has a checkbox of its own.
    const QFile::Permissions relevant = folder ? all : all & ~forWho(QFile::ExeOwner, who);
    const QFile::Permissions current = QFileInfo(path).permissions() & relevant;

    int selected = -1;
    for (const AccessLevel& level : accessLevels(folder)) {
        const QFile::Permissions bits = forWho(level.ofOwner, who);
        box->addItem(level.label, bits.toInt());
        if (bits == current)
            selected = box->count() - 1;
    }
    if (selected < 0) {
        box->addItem(u"Custom"_s, current.toInt());
        selected = box->count() - 1;
    }
    box->setCurrentIndex(selected);
    box->setEnabled(editable);

    QObject::connect(box, &QComboBox::activated, box, [box, path, relevant, error] {
        const QFile::Permissions wanted = QFile::Permissions::fromInt(box->currentData().toInt());
        const QFile::Permissions permissions = (QFileInfo(path).permissions() & ~relevant) | wanted;
        const bool ok = QFile::setPermissions(path, permissions);
        error->setVisible(!ok);
        error->setText(ok ? QString() : u"The permissions could not be changed."_s);
    });
    return box;
}

} // namespace

void showProperties(QWidget* parent, const std::vector<FileEntry>& entries, const QIcon& icon)
{
    if (entries.empty())
        return;
    const bool single = entries.size() == 1;
    const FileEntry& first = entries.front();
    const QFileInfo info(first.path);

    auto* dialog = new Dialog(single ? u"%1 Properties"_s.arg(first.name) : u"Properties"_s, parent);
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

        // Permissions, changeable by the owner.
        const bool owner = info.ownerId() == ::getuid();
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
        permissions->addRow(u"Owner"_s, valueLabel(info.owner()));
        permissions->addRow(u"Access"_s, accessBox(first.path, first.isDir, Who::Owner, owner, error));
        permissions->addRow(u"Group"_s, valueLabel(info.group()));
        permissions->addRow(u"Access"_s, accessBox(first.path, first.isDir, Who::Group, owner, error));
        permissions->addRow(u"Others"_s, accessBox(first.path, first.isDir, Who::Others, owner, error));
        if (!first.isDir) {
            auto* executable = new QCheckBox(u"Allow executing file as program"_s);
            executable->setChecked(info.permissions() & QFile::ExeOwner);
            executable->setEnabled(owner);
            QObject::connect(executable, &QCheckBox::toggled, executable, [path = first.path, error](bool on) {
                // Execute goes with read, for everyone who may read the file.
                QFile::Permissions permissions = QFileInfo(path).permissions();
                const std::pair<QFile::Permission, QFile::Permission> pairs[] = {
                    {QFile::ReadOwner, QFile::ExeOwner},
                    {QFile::ReadGroup, QFile::ExeGroup},
                    {QFile::ReadOther, QFile::ExeOther},
                };
                for (const auto& [read, exe] : pairs)
                    permissions.setFlag(exe, on && permissions.testFlag(read));
                const bool ok = QFile::setPermissions(path, permissions);
                error->setVisible(!ok);
                error->setText(ok ? QString() : u"The permissions could not be changed."_s);
            });
            permissions->addRow(QString(), executable);
        }
        layout->addWidget(error);
    }

    // Folder sizes can take a while; count them in the background.
    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    QObject::connect(dialog, &QObject::destroyed, [cancelled] { *cancelled = true; });
    auto* watcher = new QFutureWatcher<Usage>(dialog);
    QObject::connect(watcher, &QFutureWatcherBase::finished, dialog, [watcher, size, contents, selectedFolders] {
        const Usage usage = watcher->result();
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
