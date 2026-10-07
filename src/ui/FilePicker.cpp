#include "FilePicker.hpp"

#include "Application.hpp"
#include "DirectoryModel.hpp"
#include "FileSortProxy.hpp"
#include "FileView.hpp"
#include "PathBar.hpp"
#include "Sidebar.hpp"
#include "core/Location.hpp"

#include <tde/Dialog.hpp>
#include <tde/FramelessHelper.hpp>
#include <tde/HeaderBar.hpp>
#include <tde/Theme.hpp>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

using chooser::Mode;

QString defaultTitle(const chooser::Request& request)
{
    switch (request.mode) {
    case Mode::Open:
        if (request.directory)
            return request.multiple ? u"Select Folders"_s : u"Select Folder"_s;
        return request.multiple ? u"Open Files"_s : u"Open File"_s;
    case Mode::Save:
        return u"Save File"_s;
    case Mode::SaveFiles:
        return u"Save Files"_s;
    }
    return {};
}

QString defaultAcceptLabel(const chooser::Request& request)
{
    if (request.mode != Mode::Open)
        return u"Save"_s;
    return request.directory ? u"Select"_s : u"Open"_s;
}

// Mnemonics as GTK writes them, with an underscore.
QString withoutMnemonic(QString label)
{
    return label.replace(u"__"_s, u"\x01"_s).remove(u'_').replace(u'\x01', u'_');
}

} // namespace

FilePicker::FilePicker(Application& app, chooser::Request request, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
    , m_request(std::move(request))
    , m_model(new DirectoryModel(this))
    , m_proxy(new FileSortProxy(this))
{
    setObjectName(u"MainWindow"_s);
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Dialog);
    const QString title = m_request.title.isEmpty() ? defaultTitle(m_request) : m_request.title;
    setWindowTitle(title);
    resize(900, 600);
    setMinimumSize(560, 360);
    new tde::FramelessHelper(this);

    const Config::View& view = m_app.config().view;
    m_proxy->setDirectoryModel(m_model);
    m_model->setIconOverrides(m_app.config().icons);
    m_proxy->setFoldersFirst(view.foldersFirst);
    m_proxy->setCaseSensitive(view.caseSensitive);
    m_proxy->setShowHidden(view.showHidden);

    // The places on the left, under the title.
    auto* sidebarColumn = new QWidget(this);
    sidebarColumn->setObjectName(u"SidebarColumn"_s);
    sidebarColumn->setAttribute(Qt::WA_StyledBackground);
    sidebarColumn->setMinimumWidth(160);
    auto* sidebarHeader = new tde::HeaderBar(sidebarColumn);
    auto* titleLabel = new QLabel(title, sidebarHeader);
    titleLabel->setObjectName(u"DialogTitle"_s);
    titleLabel->setAlignment(Qt::AlignCenter);
    sidebarHeader->contentLayout()->addWidget(titleLabel, 1);
    m_sidebar = new Sidebar(m_app.bookmarks(), m_app.devices(), sidebarColumn);
    connect(m_sidebar, &Sidebar::locationRequested, this, &FilePicker::navigate);
    auto* sidebarLayout = new QVBoxLayout(sidebarColumn);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(sidebarHeader);
    sidebarLayout->addWidget(m_sidebar);

    // The folder on the right: where it is and the buttons above, its files, and what else
    // is to be said below.
    auto* contentColumn = new QWidget(this);
    contentColumn->setObjectName(u"ContentColumn"_s);
    contentColumn->setAttribute(Qt::WA_StyledBackground);
    auto* header = new tde::HeaderBar(contentColumn);
    QToolButton* up = tde::HeaderBar::makeButton(u"go-up"_s, u"Up (Alt+Up)"_s, header);
    up->setIcon(tde::theme::symbolicIcon(u"go-up"_s));
    connect(up, &QToolButton::clicked, this, [this] {
        if (const auto parentFolder = location::parent(m_location))
            navigate(*parentFolder);
    });
    m_pathBar = new PathBar(header);
    m_pathBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pathBar->setFixedHeight(34);
    connect(m_pathBar, &PathBar::locationClicked, this, &FilePicker::navigate);
    connect(m_pathBar, &PathBar::pathEntered, this, [this](const QString& text) {
        if (const auto url = location::fromUserInput(text, m_location))
            navigate(*url);
        else
            m_pathBar->showError();
    });
    auto* cancel = new QPushButton(u"Cancel"_s, header);
    connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
    m_accept = new QPushButton(
        m_request.acceptLabel.isEmpty() ? defaultAcceptLabel(m_request) : withoutMnemonic(m_request.acceptLabel),
        header);
    m_accept->setObjectName(u"SuggestedButton"_s);
    m_accept->setDefault(true);
    connect(m_accept, &QPushButton::clicked, this, &FilePicker::accept);
    header->contentLayout()->addWidget(up);
    header->contentLayout()->addWidget(m_pathBar, 1);
    header->contentLayout()->addWidget(cancel);
    header->contentLayout()->addWidget(m_accept);

    m_view = new FileView(m_proxy, contentColumn);
    m_view->setMode(ViewMode::List);
    m_view->setIconSizes(view.gridIconSize, view.listIconSize);
    if (m_request.multiple)
        m_view->currentView()->setSelectionMode(QAbstractItemView::ExtendedSelection);
    else
        m_view->currentView()->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_view, &FileView::activated, this, &FilePicker::activate);
    connect(m_view, &FileView::selectionChanged, this, &FilePicker::selectionChanged);

    auto* footer = new QWidget(contentColumn);
    footer->setObjectName(u"PickerFooter"_s);
    footer->setAttribute(Qt::WA_StyledBackground);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 8, 12, 8);
    footerLayout->setSpacing(10);
    if (m_request.mode == Mode::Save) {
        footerLayout->addWidget(new QLabel(u"Name"_s, footer));
        m_name = new QLineEdit(m_request.name, footer);
        m_name->setMinimumWidth(240);
        connect(m_name, &QLineEdit::textChanged, this, &FilePicker::selectionChanged);
        connect(m_name, &QLineEdit::returnPressed, this, &FilePicker::accept);
        footerLayout->addWidget(m_name, 1);
    } else {
        footerLayout->addStretch(1);
    }
    // What else the program wants to know: a box to tick, or one of some options.
    for (const chooser::Choice& choice : std::as_const(m_request.choices)) {
        if (choice.options.isEmpty()) {
            auto* box = new QCheckBox(withoutMnemonic(choice.label), footer);
            box->setChecked(choice.selected == u"true");
            footerLayout->addWidget(box);
            m_choiceWidgets.push_back(box);
            continue;
        }
        footerLayout->addWidget(new QLabel(withoutMnemonic(choice.label), footer));
        auto* options = new QComboBox(footer);
        for (const auto& [id, label] : choice.options)
            options->addItem(withoutMnemonic(label), id);
        options->setCurrentIndex(std::max(0, options->findData(choice.selected)));
        footerLayout->addWidget(options);
        m_choiceWidgets.push_back(options);
    }
    if (!m_request.filters.isEmpty()) {
        m_filter = new QComboBox(footer);
        for (const chooser::Filter& filter : std::as_const(m_request.filters))
            m_filter->addItem(filter.name);
        m_filter->setCurrentIndex(std::max(0, m_request.filter));
        connect(m_filter, &QComboBox::currentIndexChanged, this, &FilePicker::applyFilter);
        footerLayout->addWidget(m_filter);
    }
    footer->setVisible(m_name || m_filter || !m_choiceWidgets.empty());

    auto* contentLayout = new QVBoxLayout(contentColumn);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(header);
    contentLayout->addWidget(m_view, 1);
    contentLayout->addWidget(footer);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(sidebarColumn);
    splitter->addWidget(contentColumn);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({std::max(m_app.config().window.sidebarWidth, 200), 700});
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->addWidget(splitter);

    applyFilter();
    const QFileInfo folder(m_request.folder);
    navigate(folder.isDir() ? location::fromLocalPath(folder.absoluteFilePath()) : location::home());
    if (m_name) {
        m_name->setFocus();
        // The name, without what it ends in, is ready to type over.
        const qsizetype dot = m_name->text().lastIndexOf(u'.');
        m_name->setSelection(0, dot > 0 ? dot : m_name->text().size());
    } else {
        m_view->focusView();
    }
    selectionChanged();
}

void FilePicker::navigate(const QUrl& url)
{
    m_location = url;
    m_model->setLocation(url);
    m_pathBar->setLocation(url);
    m_sidebar->setCurrentLocation(url);
    selectionChanged();
}

// Only files of the kind picked show; folders, to get around, always do.
void FilePicker::applyFilter()
{
    if (m_request.directory) {
        m_proxy->setFileFilter([](const FileEntry&) { return false; });
        return;
    }
    if (!m_filter) {
        m_proxy->setFileFilter({});
        return;
    }
    const chooser::Filter filter = m_request.filters.value(m_filter->currentIndex());
    m_proxy->setFileFilter([filter](const FileEntry& entry) { return filter.matches(entry.name, entry.mimeType); });
}

std::vector<FileEntry> FilePicker::selectedEntries() const
{
    std::vector<FileEntry> entries;
    for (const QModelIndex& index : m_view->selectedRows())
        entries.push_back(m_model->entry(m_proxy->mapToSource(index)));
    return entries;
}

void FilePicker::activate(const QModelIndex& index)
{
    const FileEntry entry = m_model->entry(m_proxy->mapToSource(index));
    if (entry.isDir) {
        navigate(entry.url);
        return;
    }
    if (m_name)
        m_name->setText(entry.name);
    accept();
}

void FilePicker::selectionChanged()
{
    const std::vector<FileEntry> entries = selectedEntries();
    // A file picked while saving lends its name.
    if (m_name && entries.size() == 1 && !entries.front().isDir && sender() == m_view)
        m_name->setText(entries.front().name);

    bool ready = false;
    switch (m_request.mode) {
    case Mode::Open:
        // Folders: the one shown will do. Files: one has to be picked.
        ready
            = m_request.directory || std::ranges::any_of(entries, [](const FileEntry& entry) { return !entry.isDir; });
        break;
    case Mode::Save:
        ready = m_name && !m_name->text().trimmed().isEmpty();
        break;
    case Mode::SaveFiles:
        ready = true;
        break;
    }
    // Only into folders on this computer, or mounted on it.
    m_accept->setEnabled(ready && location::isLocal(m_location));
}

void FilePicker::accept()
{
    if (!m_accept->isEnabled())
        return;
    const QString folder = location::localPath(m_location);
    const std::vector<FileEntry> entries = selectedEntries();

    switch (m_request.mode) {
    case Mode::Open: {
        QList<QUrl> urls;
        for (const FileEntry& entry : entries) {
            if (entry.isDir == m_request.directory)
                urls << QUrl::fromLocalFile(entry.path);
        }
        // A single folder picked while opening files is gone into instead.
        if (urls.isEmpty() && !m_request.directory && entries.size() == 1 && entries.front().isDir) {
            navigate(entries.front().url);
            return;
        }
        if (urls.isEmpty() && m_request.directory)
            urls << QUrl::fromLocalFile(folder);
        if (!urls.isEmpty())
            finish(true, urls);
        return;
    }
    case Mode::Save: {
        const QString name = m_name->text().trimmed();
        const QString path = QDir(folder).absoluteFilePath(name);
        const QFileInfo info(path);
        if (info.isDir()) {
            navigate(location::fromLocalPath(path));
            m_name->clear();
            return;
        }
        if (info.exists()
            && !tde::Dialog::confirm(this, u"Replace File"_s, u"Replace “%1”?"_s.arg(info.fileName()),
                u"A file of that name is already there; saving replaces what it holds."_s, u"Replace"_s)) {
            return;
        }
        finish(true, {QUrl::fromLocalFile(path)});
        return;
    }
    case Mode::SaveFiles: {
        QList<QUrl> urls;
        for (const QString& name : std::as_const(m_request.files))
            urls << QUrl::fromLocalFile(QDir(folder).absoluteFilePath(name));
        finish(true, urls);
        return;
    }
    }
}

void FilePicker::finish(bool picked, QList<QUrl> urls)
{
    if (m_done)
        return;
    m_done = true;
    chooser::Answer answer;
    answer.urls = std::move(urls);
    answer.filter = m_filter ? m_filter->currentIndex() : -1;
    for (size_t i = 0; i < m_choiceWidgets.size(); ++i) {
        chooser::Choice choice = m_request.choices[qsizetype(i)];
        if (auto* box = qobject_cast<QCheckBox*>(m_choiceWidgets[i]))
            choice.selected = box->isChecked() ? u"true"_s : u"false"_s;
        else if (auto* options = qobject_cast<QComboBox*>(m_choiceWidgets[i]))
            choice.selected = options->currentData().toString();
        answer.choices << choice;
    }
    emit finished(picked, answer);
    close();
}

void FilePicker::closeEvent(QCloseEvent* event)
{
    // Closed by its window button: given up on.
    finish(false);
    event->accept();
}

void FilePicker::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        finish(false);
        return;
    }
    if (event->modifiers() == Qt::AltModifier && event->key() == Qt::Key_Up) {
        if (const auto parentFolder = location::parent(m_location))
            navigate(*parentFolder);
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace ariadne
