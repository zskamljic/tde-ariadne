#include "MainWindow.hpp"

#include "Application.hpp"
#include "BatchRenameDialog.hpp"
#include "Dialogs.hpp"
#include "DirectoryModel.hpp"
#include "FileManagerService.hpp"
#include "FileSortProxy.hpp"
#include "FileView.hpp"
#include "Jobs.hpp"
#include "JobsButton.hpp"
#include "OpenWithDialog.hpp"
#include "PathBar.hpp"
#include "PropertiesDialog.hpp"
#include "Sidebar.hpp"
#include "core/Applications.hpp"
#include "core/Archives.hpp"
#include "core/ClipboardFormat.hpp"
#include "core/CustomActions.hpp"
#include "core/DefaultFileManager.hpp"
#include "core/DeviceMonitor.hpp"
#include "core/FileOperations.hpp"
#include "core/Location.hpp"
#include "core/Terminal.hpp"

#include <tde/DesktopConfig.hpp>
#include <tde/Dialog.hpp>
#include <tde/FramelessHelper.hpp>
#include <tde/HeaderBar.hpp>
#include <tde/Theme.hpp>
#include <tde/Toast.hpp>
#include <tde/WindowButtons.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrentRun>
#include <QtMath>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

struct SortOption {
    QString label;
    SortKey key;
    bool descending;
};

const SortOption sortOptions[] = {
    {u"A–Z"_s, SortKey::Name, false},
    {u"Z–A"_s, SortKey::Name, true},
    {u"Last Modified"_s, SortKey::Modified, true},
    {u"First Modified"_s, SortKey::Modified, false},
    {u"Size"_s, SortKey::Size, true},
    {u"Type"_s, SortKey::Type, false},
};

int sortData(SortKey key, bool descending)
{
    return static_cast<int>(key) * 2 + (descending ? 1 : 0);
}

QAction* makeAction(QWidget* owner, const QString& text, std::initializer_list<QKeySequence> shortcuts = {})
{
    auto* action = new QAction(text, owner);
    action->setShortcuts(QList<QKeySequence>(shortcuts));
    owner->addAction(action);
    return action;
}

} // namespace

MainWindow::MainWindow(Application& app, const QUrl& location, const QString& selectName, QWidget* parent)
    : QWidget(parent)
    , m_app(app)
    , m_model(new DirectoryModel(this))
    , m_proxy(new FileSortProxy(this))
    , m_selectAfterLoad(selectName)
{
    setObjectName(u"MainWindow"_s);
    setAttribute(Qt::WA_StyledBackground);
    setMinimumSize(560, 360);
    new tde::FramelessHelper(this);

    const Config::View& view = m_app.config().view;
    m_proxy->setDirectoryModel(m_model);
    m_model->setIconOverrides(m_app.config().icons);
    m_proxy->setFoldersFirst(view.foldersFirst);
    m_proxy->setCaseSensitive(view.caseSensitive);
    m_proxy->setShowHidden(view.showHidden);
    m_model->setExpandableFolders(view.expandableFolders);

    createActions();

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(createSidebarColumn());
    splitter->addWidget(createContentColumn());
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({m_app.config().window.sidebarWidth, m_app.config().window.width});
    connect(splitter, &QSplitter::splitterMoved, this, [this, splitter] {
        m_app.updateConfig([&](Config& config) { config.window.sidebarWidth = splitter->sizes().value(0); });
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->addWidget(splitter);

    connect(m_model, &DirectoryModel::loaded, this, &MainWindow::onLoaded);
    connect(m_model, &DirectoryModel::searchFinished, this, &MainWindow::updatePlaceholder);
    connect(m_model, &DirectoryModel::renameRequested, this, &MainWindow::renameItem);
    connect(m_model, &DirectoryModel::loadFailed, this, [this](const QString& message) {
        m_loadError = message;
        updatePlaceholder();
    });
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(m_proxy, signal, this, &MainWindow::updatePlaceholder);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &MainWindow::updatePlaceholder);
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, &MainWindow::updatePlaceholder);
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this] {
        // Select an item this window just created, once it shows up.
        if (m_selectWhenCreated.isEmpty())
            return;
        const int row = m_model->rowOf(m_selectWhenCreated);
        const QModelIndex index = row >= 0 ? m_proxy->mapFromSource(m_model->index(row, 0)) : QModelIndex();
        if (index.isValid()) {
            m_selectWhenCreated.clear();
            m_view->selectRow(index);
            m_view->focusView();
        }
    });

    m_view->setIconSizes(view.gridIconSize, view.listIconSize);
    m_view->setExpandableFolders(view.expandableFolders);
    placeWindowButtons();
    setupCustomShortcuts();
    setLocation(location); // also applies the folder's view settings
}

void MainWindow::placeWindowButtons()
{
    const auto& buttons = tde::desktop().windowButtons;
    for (QWidget* slot : {m_leftButtonSlot, m_rightButtonSlot}) {
        for (auto* old : slot->findChildren<tde::WindowButtons*>(Qt::FindDirectChildrenOnly)) {
            old->hide();
            old->deleteLater();
        }
    }
    QWidget* slot = buttons.side == tde::ButtonSide::Left ? m_leftButtonSlot : m_rightButtonSlot;
    slot->layout()->addWidget(new tde::WindowButtons(buttons.order, slot));
    m_leftButtonSlot->setVisible(slot == m_leftButtonSlot);
    m_rightButtonSlot->setVisible(slot == m_rightButtonSlot);
}

void MainWindow::applyDesktopConfig()
{
    // Colours, corners and icons follow the theme by themselves; the buttons are laid out anew.
    placeWindowButtons();
    update();
}

void MainWindow::applyConfig(const Config& config)
{
    // Through the actions, as if chosen in the menus.
    m_showHiddenAction->setChecked(config.view.showHidden);
    m_foldersFirstAction->setChecked(config.view.foldersFirst);
    m_caseSensitiveAction->setChecked(config.view.caseSensitive);
    m_model->setIconOverrides(config.icons);
    m_view->setIconSizes(config.view.gridIconSize, config.view.listIconSize);
    m_model->setExpandableFolders(config.view.expandableFolders);
    m_view->setExpandableFolders(config.view.expandableFolders);
    setupCustomShortcuts();
    // Folders shown the default way take the new default.
    if (!m_model->isSearching() && m_model->searchQuery().isEmpty())
        applyFolderView(m_app.folderView(m_location));
}

void MainWindow::createActions()
{
    m_backAction = makeAction(this, u"Back"_s, {QKeySequence::Back});
    m_forwardAction = makeAction(this, u"Forward"_s, {QKeySequence::Forward});
    m_upAction = makeAction(this, u"Open Parent Folder"_s, {QKeySequence(Qt::ALT | Qt::Key_Up)});
    m_homeAction = makeAction(this, u"Home"_s, {QKeySequence(Qt::ALT | Qt::Key_Home)});
    m_editLocationAction = makeAction(this, u"Enter Location"_s, {QKeySequence(Qt::CTRL | Qt::Key_L)});
    m_reloadAction = makeAction(this, u"Reload"_s, {QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::Key_R)});
    m_showHiddenAction = makeAction(this, u"Show Hidden Files"_s, {QKeySequence(Qt::CTRL | Qt::Key_H)});
    m_showHiddenAction->setCheckable(true);
    m_showHiddenAction->setChecked(m_proxy->showHidden());
    m_gridAction = makeAction(this, u"Grid"_s, {QKeySequence(Qt::CTRL | Qt::Key_1)});
    m_listAction = makeAction(this, u"List"_s, {QKeySequence(Qt::CTRL | Qt::Key_2)});
    auto* modeGroup = new QActionGroup(this);
    for (QAction* action : {m_gridAction, m_listAction}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    m_zoomInAction = makeAction(this, u"Zoom In"_s, {QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    m_zoomOutAction = makeAction(this, u"Zoom Out"_s, {QKeySequence::ZoomOut});
    m_zoomResetAction = makeAction(this, u"Reset Zoom"_s, {QKeySequence(Qt::CTRL | Qt::Key_0)});
    m_bookmarkAction = makeAction(this, u"Bookmark this Location"_s, {QKeySequence(Qt::CTRL | Qt::Key_D)});
    m_trashAction = makeAction(this, u"Move to Trash"_s, {QKeySequence::Delete});
    m_deleteAction = makeAction(this, u"Delete Permanently…"_s, {QKeySequence(Qt::SHIFT | Qt::Key_Delete)});
    m_renameAction = makeAction(this, u"Rename…"_s, {QKeySequence(Qt::Key_F2)});
    m_propertiesAction = makeAction(this, u"Properties"_s, {QKeySequence(Qt::CTRL | Qt::Key_I)});
    m_searchAction = makeAction(this, u"Search"_s, {QKeySequence::Find});
    m_searchAction->setCheckable(true);
    m_cutAction = makeAction(this, u"Cut"_s, {QKeySequence::Cut});
    m_copyAction = makeAction(this, u"Copy"_s, {QKeySequence::Copy});
    m_pasteAction = makeAction(this, u"Paste"_s, {QKeySequence::Paste});
    m_undoAction = makeAction(this, u"Undo"_s, {QKeySequence::Undo});
    m_newWindowAction = makeAction(this, u"New Window"_s, {QKeySequence::New});
    m_newFolderAction = makeAction(this, u"New Folder…"_s, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)});
    m_closeAction = makeAction(this, u"Close Window"_s, {QKeySequence(Qt::CTRL | Qt::Key_W)});
    m_quitAction = makeAction(this, u"Quit"_s, {QKeySequence(Qt::CTRL | Qt::Key_Q)});

    m_foldersFirstAction = new QAction(u"Folders First"_s, this);
    m_foldersFirstAction->setCheckable(true);
    m_foldersFirstAction->setChecked(m_proxy->foldersFirst());
    connect(m_foldersFirstAction, &QAction::toggled, this, [this](bool foldersFirst) {
        m_proxy->setFoldersFirst(foldersFirst);
        m_app.updateConfig([&](Config& config) { config.view.foldersFirst = foldersFirst; });
    });

    m_caseSensitiveAction = new QAction(u"Case Sensitive"_s, this);
    m_caseSensitiveAction->setCheckable(true);
    m_caseSensitiveAction->setChecked(m_proxy->isCaseSensitive());
    connect(m_caseSensitiveAction, &QAction::toggled, this, [this](bool caseSensitive) {
        m_proxy->setCaseSensitive(caseSensitive);
        m_app.updateConfig([&](Config& config) { config.view.caseSensitive = caseSensitive; });
    });

    m_sortGroup = new QActionGroup(this);
    m_sortGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    for (const SortOption& option : sortOptions) {
        auto* action = new QAction(option.label, this);
        action->setCheckable(true);
        action->setData(sortData(option.key, option.descending));
        m_sortGroup->addAction(action);
        connect(action, &QAction::triggered, this,
            [this, key = option.key, descending = option.descending] { setSort(key, descending); });
    }

    connect(m_backAction, &QAction::triggered, this, &MainWindow::goBack);
    connect(m_forwardAction, &QAction::triggered, this, &MainWindow::goForward);
    connect(m_upAction, &QAction::triggered, this, &MainWindow::goUp);
    connect(m_homeAction, &QAction::triggered, this, [this] { navigate(location::home()); });
    connect(m_editLocationAction, &QAction::triggered, this, [this] { m_pathBar->startEditing(); });
    connect(m_reloadAction, &QAction::triggered, this, [this] { m_model->reload(); });
    connect(m_searchAction, &QAction::triggered, this, [this](bool on) {
        if (on)
            openSearch({});
        else
            closeSearch(true);
    });
    connect(m_showHiddenAction, &QAction::toggled, this, [this](bool show) {
        m_proxy->setShowHidden(show);
        if (!m_model->searchQuery().isEmpty())
            m_searchTimer.start(0); // hidden files may now be searched too
        m_app.updateConfig([&](Config& config) { config.view.showHidden = show; });
    });
    connect(m_gridAction, &QAction::triggered, this, [this] { setViewMode(ViewMode::Grid); });
    connect(m_listAction, &QAction::triggered, this, [this] { setViewMode(ViewMode::List); });
    connect(m_zoomInAction, &QAction::triggered, this, [this] { m_view->zoom(1); });
    connect(m_zoomOutAction, &QAction::triggered, this, [this] { m_view->zoom(-1); });
    connect(m_zoomResetAction, &QAction::triggered, this, [this] {
        const Config::View defaults;
        m_view->setIconSizes(defaults.gridIconSize, defaults.listIconSize);
    });
    connect(m_bookmarkAction, &QAction::triggered, this, [this] {
        if (location::isLocal(m_location))
            m_app.bookmarks().add(m_location);
    });
    connect(m_newWindowAction, &QAction::triggered, this, [this] { m_app.openWindow(m_location); });
    connect(m_newFolderAction, &QAction::triggered, this, [this] { createItem(NewItem::Folder); });
    connect(m_trashAction, &QAction::triggered, this, &MainWindow::trashSelection);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteSelection);
    connect(m_renameAction, &QAction::triggered, this, &MainWindow::renameSelection);
    connect(m_propertiesAction, &QAction::triggered, this, [this] { showPropertiesOf(selectedEntries()); });
    connect(m_cutAction, &QAction::triggered, this, [this] { putOnClipboard(true); });
    connect(m_copyAction, &QAction::triggered, this, [this] { putOnClipboard(false); });
    connect(m_pasteAction, &QAction::triggered, this, [this] { paste({}); });
    connect(m_undoAction, &QAction::triggered, this, [this] { m_app.undo(*this); });
    connect(&m_app, &Application::undoChanged, this, &MainWindow::updateUndo);
    updateUndo();
    connect(m_closeAction, &QAction::triggered, this, &QWidget::close);
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);
}

QWidget* MainWindow::createSidebarColumn()
{
    auto* column = new QWidget(this);
    column->setObjectName(u"SidebarColumn"_s);
    column->setAttribute(Qt::WA_StyledBackground);
    column->setMinimumWidth(160);

    auto* header = new tde::HeaderBar(column);
    m_leftButtonSlot = new QWidget(header);
    (new QHBoxLayout(m_leftButtonSlot))->setContentsMargins(0, 0, 0, 0);
    header->contentLayout()->addWidget(m_leftButtonSlot);
    header->contentLayout()->addStretch(1);
    QToolButton* menuButton = tde::HeaderBar::makeButton(u"open-menu"_s, u"Main Menu"_s, header);
    menuButton->setMenu(createAppMenu());
    menuButton->setPopupMode(QToolButton::InstantPopup);
    header->contentLayout()->addWidget(menuButton);

    m_sidebar = new Sidebar(m_app.bookmarks(), m_app.devices(), column);
    connect(m_sidebar, &Sidebar::locationRequested, this, &MainWindow::navigate);
    connect(m_sidebar, &Sidebar::newWindowRequested, this, [this](const QUrl& url) { m_app.openWindow(url); });
    connect(m_sidebar, &Sidebar::mountRequested, this, &MainWindow::mountDevice);
    connect(m_sidebar, &Sidebar::unmountRequested, this, [this](const QString& id) { unmountDevice(id, false); });
    connect(m_sidebar, &Sidebar::ejectRequested, this, [this](const QString& id) { unmountDevice(id, true); });
    connect(m_sidebar, &Sidebar::emptyTrashRequested, this, &MainWindow::emptyTrash);
    connect(m_sidebar, &Sidebar::filesDropped, this, &MainWindow::drop);
    connect(m_sidebar, &Sidebar::trashDropped, this, &MainWindow::trashPaths);

    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_sidebar);
    return column;
}

QWidget* MainWindow::createContentColumn()
{
    auto* column = new QWidget(this);
    column->setObjectName(u"ContentColumn"_s);
    column->setAttribute(Qt::WA_StyledBackground);

    auto* header = new tde::HeaderBar(column);
    QHBoxLayout* headerLayout = header->contentLayout();

    // A default action overrides the button's icon, so the icons live on the actions.
    m_backAction->setIcon(tde::theme::symbolicIcon(u"go-previous"_s));
    m_forwardAction->setIcon(tde::theme::symbolicIcon(u"go-next"_s));
    QToolButton* backButton = tde::HeaderBar::makeButton({}, {}, header);
    backButton->setDefaultAction(m_backAction);
    QToolButton* forwardButton = tde::HeaderBar::makeButton({}, {}, header);
    forwardButton->setDefaultAction(m_forwardAction);
    headerLayout->addWidget(backButton);
    headerLayout->addWidget(forwardButton);

    m_pathBar = new PathBar(header);
    m_searchEntry = new QLineEdit(header);
    m_searchEntry->setObjectName(u"SearchEntry"_s);
    m_searchEntry->setClearButtonEnabled(true);
    m_searchEntry->addAction(tde::theme::symbolicIcon(u"edit-find"_s), QLineEdit::LeadingPosition);
    m_searchEntry->installEventFilter(this);
    m_locationStack = new QStackedWidget(header);
    m_locationStack->addWidget(m_pathBar);
    m_locationStack->addWidget(m_searchEntry);
    m_locationStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_locationStack->setFixedHeight(34);
    headerLayout->addWidget(m_locationStack, 1);
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(200);
    connect(m_searchEntry, &QLineEdit::textEdited, this, [this] { m_searchTimer.start(); });
    connect(&m_searchTimer, &QTimer::timeout, this, &MainWindow::runSearch);
    QToolButton* searchButton = tde::HeaderBar::makeButton({}, {}, header);
    m_searchAction->setIcon(tde::theme::symbolicIcon(u"edit-find"_s));
    m_searchAction->setToolTip(u"Search (Ctrl+F)"_s);
    searchButton->setDefaultAction(m_searchAction);
    connect(m_pathBar, &PathBar::locationClicked, this, &MainWindow::navigate);
    connect(m_pathBar, &PathBar::pathEntered, this, &MainWindow::onPathEntered);
    connect(m_pathBar, &PathBar::editingCancelled, this, [this] { m_view->focusView(); });

    m_viewButton = tde::HeaderBar::makeButton(u"view-list"_s, {}, header);
    m_viewButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_viewButton->setMenu(createViewMenu());
    connect(m_viewButton, &QToolButton::clicked, this,
        [this] { setViewMode(m_view->mode() == ViewMode::Grid ? ViewMode::List : ViewMode::Grid); });
    headerLayout->addWidget(searchButton);
    headerLayout->addWidget(new JobsButton(m_app.jobs(), header));
    headerLayout->addWidget(m_viewButton);

    m_rightButtonSlot = new QWidget(header);
    (new QHBoxLayout(m_rightButtonSlot))->setContentsMargins(6, 0, 0, 0);
    headerLayout->addWidget(m_rightButtonSlot);

    m_view = new FileView(m_proxy, column);
    m_toast = new tde::Toast(m_view);
    connect(m_view, &FileView::activated, this, &MainWindow::activate);
    connect(m_view, &FileView::middleClicked, this, [this](const QModelIndex& index) {
        const FileEntry entry = entryAt(index);
        if (const auto folder = folderOf(entry))
            m_app.openWindow(*folder);
        else
            openFile(entry);
    });
    connect(m_view, &FileView::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_view, &FileView::headerClicked, this, &MainWindow::onHeaderClicked);
    connect(m_view, &FileView::iconSizesChanged, this, [this](int grid, int list) {
        m_model->setThumbnailSize(qCeil(grid * devicePixelRatioF()));
        m_app.updateConfig([&](Config& config) {
            config.view.gridIconSize = grid;
            config.view.listIconSize = list;
        });
    });

    connect(m_view, &FileView::dropped, this, &MainWindow::drop);
    connect(m_view, &FileView::textTyped, this, [this](const QString& text) {
        if (m_locationStack->currentWidget() == m_searchEntry) {
            m_searchEntry->insert(text);
            m_searchEntry->setFocus();
            m_searchTimer.start();
        } else {
            openSearch(text);
        }
    });
    connect(m_view, &FileView::selectionChanged, this, [this] {
        updateStatus();
        updateTrashBar();
    });

    // Shown above the files while in the Trash.
    m_trashBar = new QWidget(column);
    m_trashBar->setObjectName(u"InfoBar"_s);
    m_trashBar->setAttribute(Qt::WA_StyledBackground);
    auto* trashLayout = new QHBoxLayout(m_trashBar);
    trashLayout->setContentsMargins(12, 6, 8, 6);
    auto* trashLabel = new QLabel(u"Trashed items can be restored to where they came from."_s, m_trashBar);
    trashLabel->setObjectName(u"AboutDetails"_s);
    m_restoreButton = new QPushButton(u"Restore"_s, m_trashBar);
    m_emptyTrashButton = new QPushButton(u"Empty Trash…"_s, m_trashBar);
    m_emptyTrashButton->setObjectName(u"DestructiveButton"_s);
    trashLayout->addWidget(trashLabel, 1);
    trashLayout->addWidget(m_restoreButton);
    trashLayout->addWidget(m_emptyTrashButton);
    connect(m_restoreButton, &QPushButton::clicked, this, &MainWindow::restoreSelection);
    connect(m_emptyTrashButton, &QPushButton::clicked, this, &MainWindow::emptyTrash);
    m_trashBar->hide();

    // Inside an archive: say so, and offer to unpack it.
    m_archiveBar = new QWidget(column);
    m_archiveBar->setObjectName(u"InfoBar"_s);
    m_archiveBar->setAttribute(Qt::WA_StyledBackground);
    auto* archiveLayout = new QHBoxLayout(m_archiveBar);
    archiveLayout->setContentsMargins(12, 6, 8, 6);
    m_archiveLabel = new QLabel(m_archiveBar);
    m_archiveLabel->setObjectName(u"AboutDetails"_s);
    auto* extractAll = new QPushButton(u"Extract All…"_s, m_archiveBar);
    archiveLayout->addWidget(m_archiveLabel, 1);
    archiveLayout->addWidget(extractAll);
    connect(extractAll, &QPushButton::clicked, this, [this] {
        if (const auto place = location::archivePlace(m_location))
            extractArchives({place->file}, true);
    });
    m_archiveBar->hide();

    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_trashBar);
    layout->addWidget(m_archiveBar);
    layout->addWidget(m_view);
    return column;
}

QMenu* MainWindow::createAppMenu()
{
    auto* menu = new QMenu(this);
    menu->addAction(m_newWindowAction);
    menu->addSeparator();
    menu->addAction(m_undoAction);
    menu->addSeparator();
    menu->addAction(m_showHiddenAction);
    menu->addAction(m_bookmarkAction);
    menu->addAction(m_reloadAction);
    menu->addSeparator();
    if (gvfs::isAvailable())
        menu->addAction(u"Connect to Server…"_s, this, &MainWindow::connectToServer);
    if (!defaults::status().complete())
        menu->addAction(u"Make Default File Manager…"_s, this, &MainWindow::makeDefault);
    menu->addAction(u"About Ariadne"_s, this,
        [this] { tde::Dialog::showAbout(this, u"Ariadne"_s, u"A file manager, part of TDE."_s); });
    menu->addAction(m_quitAction);
    return menu;
}

QMenu* MainWindow::createViewMenu()
{
    auto* menu = new QMenu(this);
    menu->addSection(u"View"_s);
    menu->addAction(m_gridAction);
    menu->addAction(m_listAction);
    menu->addSection(u"Sort"_s);
    menu->addActions(m_sortGroup->actions());
    menu->addSeparator();
    menu->addAction(m_foldersFirstAction);
    menu->addAction(m_caseSensitiveAction);
    menu->addSeparator();
    // View and sort changes are remembered for the folder they are made in.
    menu->addAction(u"Use These Settings for All Folders"_s, this, [this] {
        const FolderView view = currentFolderView();
        m_app.updateConfig([&](Config& config) {
            config.view.mode = view.mode;
            config.view.sortKey = view.sortKey;
            config.view.sortDescending = view.sortDescending;
        });
        m_app.forgetFolderView(m_location);
        m_toast->showMessage(u"New folders are shown like this one now."_s);
    });
    menu->addAction(u"Reset to Default"_s, this, [this] {
        m_app.forgetFolderView(m_location);
        applyFolderView(m_app.folderView(m_location));
    });
    menu->addSeparator();
    menu->addAction(m_showHiddenAction);
    menu->addSeparator();
    menu->addAction(m_zoomInAction);
    menu->addAction(m_zoomOutAction);
    menu->addAction(m_zoomResetAction);
    return menu;
}

void MainWindow::navigate(const QUrl& url)
{
    if (url == m_location)
        return;
    m_backStack << m_location;
    m_forwardStack.clear();
    setLocation(url);
}

void MainWindow::setLocation(const QUrl& url)
{
    closeSearch(false); // the new folder gets listed anyway
    const QUrl previous = m_location;
    m_location = url;
    m_loadError.clear();

    // Going up selects the folder we came from.
    if (m_selectAfterLoad.isEmpty() && !previous.isEmpty() && location::isAncestorOf(url, previous)) {
        QUrl child = previous;
        for (auto parent = location::parent(child); parent && *parent != url; parent = location::parent(child))
            child = *parent;
        m_selectAfterLoad = QFileInfo(location::localPath(child)).fileName();
    }

    m_view->setPlaceholder({}, {});
    applyFolderView(m_app.folderView(url));
    m_model->setLocation(url);
    m_pathBar->setLocation(url);
    m_sidebar->setCurrentLocation(url);
    setWindowTitle(location::displayName(url));
    updateNavigation();
    updateTrashBar();
    updateStatus();
    if (location::isTrash(url))
        m_view->setDropDirectory({}, true);
    else
        m_view->setDropDirectory(canCreateHere() ? location::localPath(url) : QString(), false);
}

void MainWindow::goBack()
{
    if (m_backStack.isEmpty())
        return;
    m_forwardStack << m_location;
    setLocation(m_backStack.takeLast());
}

void MainWindow::goForward()
{
    if (m_forwardStack.isEmpty())
        return;
    m_backStack << m_location;
    setLocation(m_forwardStack.takeLast());
}

void MainWindow::goUp()
{
    if (const auto parent = location::parent(m_location))
        navigate(*parent);
}

void MainWindow::updateNavigation()
{
    m_backAction->setEnabled(!m_backStack.isEmpty());
    m_forwardAction->setEnabled(!m_forwardStack.isEmpty());
    m_upAction->setEnabled(location::parent(m_location).has_value());
    m_bookmarkAction->setEnabled(location::isLocal(m_location));
    m_newFolderAction->setEnabled(canCreateHere());
}

void MainWindow::onLoaded()
{
    if (!m_selectAfterLoad.isEmpty()) {
        const int row = m_model->rowOf(m_selectAfterLoad);
        m_selectAfterLoad.clear();
        const QModelIndex index = row >= 0 ? m_proxy->mapFromSource(m_model->index(row, 0)) : QModelIndex();
        if (index.isValid())
            m_view->selectRow(index);
    } else if (!m_model->loadsGradually()) { // by then, the user may have scrolled
        m_view->currentView()->scrollToTop();
    }
    updatePlaceholder();
    if (!m_pathBar->isEditing())
        m_view->focusView();
}

void MainWindow::updatePlaceholder()
{
    updateTrashBar();
    if (!m_loadError.isEmpty()) {
        m_view->setPlaceholder(u"dialog-error"_s, m_loadError);
    } else if (!m_model->searchQuery().isEmpty() && m_proxy->rowCount() == 0) {
        m_view->setPlaceholder(u"edit-find"_s,
            m_model->isSearching() ? u"Searching…"_s : u"No results for “%1”"_s.arg(m_model->searchQuery()));
    } else if (m_model->isLoading() || m_proxy->rowCount() > 0) {
        m_view->setPlaceholder({}, {});
    } else if (m_location == location::trash()) {
        m_view->setPlaceholder(u"user-trash"_s, u"Trash is Empty"_s);
    } else {
        m_view->setPlaceholder(u"folder"_s, u"Folder is Empty"_s);
    }
}

void MainWindow::setViewMode(ViewMode mode, bool remember)
{
    m_view->setMode(mode);
    (mode == ViewMode::Grid ? m_gridAction : m_listAction)->setChecked(true);
    const bool grid = mode == ViewMode::Grid;
    m_viewButton->setIcon(tde::theme::symbolicIcon(grid ? u"view-list"_s : u"view-grid"_s));
    m_viewButton->setToolTip(grid ? u"Switch to List View"_s : u"Switch to Grid View"_s);
    if (remember)
        rememberFolderView();
}

void MainWindow::setSort(SortKey key, bool descending, bool remember)
{
    m_proxy->setSort(key, descending);
    m_view->setSortIndicator(key, descending);
    const int wanted = sortData(key, descending);
    for (QAction* action : m_sortGroup->actions())
        action->setChecked(action->data().toInt() == wanted);
    if (remember)
        rememberFolderView();
}

FolderView MainWindow::currentFolderView() const
{
    return {m_view->mode(), m_proxy->sortKey(), m_proxy->isDescending()};
}

void MainWindow::applyFolderView(const FolderView& view)
{
    setViewMode(view.mode, false);
    setSort(view.sortKey, view.sortDescending, false);
}

void MainWindow::rememberFolderView()
{
    // Search results are no folder of their own.
    if (m_model->searchQuery().isEmpty())
        m_app.rememberFolderView(m_location, currentFolderView());
}

void MainWindow::onHeaderClicked(int column)
{
    SortKey key = SortKey::Name;
    switch (column) {
    case DirectoryModel::SizeColumn:
        key = SortKey::Size;
        break;
    case DirectoryModel::TypeColumn:
        key = SortKey::Type;
        break;
    case DirectoryModel::ModifiedColumn:
        key = SortKey::Modified;
        break;
    default:
        break;
    }
    // Clicking the current column flips the order; a new column starts with its natural order.
    const bool naturalDescending = key == SortKey::Modified || key == SortKey::Size;
    setSort(key, key == m_proxy->sortKey() ? !m_proxy->isDescending() : naturalDescending);
}

void MainWindow::onPathEntered(const QString& text)
{
    const auto url = location::fromUserInput(text, m_location);
    if (!url) {
        m_pathBar->showError();
        return;
    }

    const QFileInfo info(location::localPath(*url));
    if (info.isDir()) {
        navigate(*url);
        m_view->focusView();
    } else if (info.exists()) {
        // A file: show it selected in its folder.
        m_selectAfterLoad = info.fileName();
        navigate(*location::parent(*url));
        m_view->focusView();
    } else {
        m_pathBar->showError();
    }
}

FileEntry MainWindow::entryAt(const QModelIndex& proxyIndex) const
{
    return m_model->entry(m_proxy->mapToSource(proxyIndex));
}

std::vector<FileEntry> MainWindow::selectedEntries() const
{
    std::vector<FileEntry> entries;
    for (const QModelIndex& index : m_view->selectedRows())
        entries.push_back(entryAt(index));
    return entries;
}

void MainWindow::activate(const QModelIndex& index)
{
    std::vector<FileEntry> entries = selectedEntries();
    const FileEntry clicked = entryAt(index);
    if (std::ranges::find(entries, clicked.path, &FileEntry::path) == entries.end())
        entries = {clicked};

    if (entries.size() == 1) {
        if (const auto folder = folderOf(clicked))
            navigate(*folder);
        else
            openFile(clicked);
        return;
    }
    for (const FileEntry& entry : entries) {
        if (const auto folder = folderOf(entry))
            m_app.openWindow(*folder);
        else
            openFile(entry);
    }
}

std::optional<QUrl> MainWindow::folderOf(const FileEntry& entry) const
{
    if (entry.isDir)
        return entry.url;
    // Archives open like folders, unless that is turned off; ones inside archives cannot.
    if (m_app.config().view.archivesAsFolders && location::isLocal(entry.url) && archives::isArchive(entry.mimeType))
        return location::archiveRoot(entry.path);
    return std::nullopt;
}

void MainWindow::openFile(const FileEntry& entry)
{
    // A copy, unpacked for the purpose; the archive stays as it is.
    if (location::isArchive(entry.url)) {
        extractCopies({entry}, [this](const QStringList& paths) {
            const QMimeDatabase mimeDatabase;
            for (const QString& path : paths)
                openInApplication(describeEntry(QFileInfo(path), location::fromLocalPath(path), false, mimeDatabase));
        });
        return;
    }
    // Programs run; scripts can be run or edited, so they ask. Nothing runs from the trash.
    const Executable kind = location::isTrash(m_location) ? Executable::No : executableKind(entry.path, entry.mimeType);
    bool run = kind == Executable::Program;
    bool inTerminal = false;
    if (kind == Executable::Script) {
        switch (dialogs::askScriptAction(this, entry.name)) {
        case dialogs::ScriptAction::Cancel:
            return;
        case dialogs::ScriptAction::Open:
            break;
        case dialogs::ScriptAction::RunInTerminal:
            inTerminal = true;
            [[fallthrough]];
        case dialogs::ScriptAction::Run:
            run = true;
            break;
        }
    }
    if (run) {
        if (const auto started = runExecutable(entry.path, inTerminal, tde::desktop().terminal); !started)
            m_toast->showMessage(started.error());
        return;
    }
    openInApplication(entry);
}

void MainWindow::openInApplication(const FileEntry& entry)
{
    // The default application from mimeapps.list, like the rest of the desktop would pick.
    const Applications applications = Applications::load();
    const DesktopApp* app = applications.defaultFor(entry.mimeType);
    if (!app)
        app = applications.forMimeType(entry.mimeType).value(0);
    if (app) {
        if (const auto launched = Applications::launch(*app, {entry.path}, tde::desktop().terminal); !launched)
            m_toast->showMessage(launched.error());
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(entry.path)))
        m_toast->showMessage(u"There is no application for opening “%1”."_s.arg(entry.name));
}

namespace {

std::vector<ActionTarget> targetsOf(const std::vector<FileEntry>& entries)
{
    std::vector<ActionTarget> targets;
    for (const FileEntry& entry : entries)
        targets.push_back({entry.path, entry.mimeType});
    return targets;
}

QIcon actionIcon(const QString& icon)
{
    if (icon.isEmpty())
        return {};
    return QFileInfo(icon).isAbsolute() ? QIcon(icon) : tde::theme::themeIcon({icon});
}

} // namespace

void MainWindow::addCustomActions(QMenu& menu, const std::vector<FileEntry>& entries)
{
    const std::vector<ActionTarget> targets = targetsOf(entries);
    const QMimeDatabase mimes;
    bool first = true;
    for (const CustomAction& action : m_app.config().actions) {
        if (!appliesTo(action, targets, mimes))
            continue;
        if (std::exchange(first, false))
            menu.addSeparator();
        QAction* item = menu.addAction(
            actionIcon(action.icon), action.name, this, [this, action, targets] { runCustomAction(action, targets); });
        // Shown as a hint; pressing it is handled by the window's own action.
        item->setShortcut(QKeySequence(action.shortcut));
        item->setShortcutContext(Qt::WidgetShortcut);
    }
}

void MainWindow::runCustomAction(const CustomAction& action, const std::vector<ActionTarget>& targets)
{
    const QString folder = location::localPath(m_location);
    if (const auto started = runAction(action, targets, folder, tde::desktop().terminal); !started)
        m_toast->showMessage(started.error());
}

void MainWindow::setupCustomShortcuts()
{
    for (QAction* old : std::exchange(m_customShortcuts, {})) {
        removeAction(old);
        old->deleteLater();
    }
    for (const CustomAction& action : m_app.config().actions) {
        if (action.shortcut.isEmpty())
            continue;
        const QKeySequence keys(action.shortcut);
        if (keys.isEmpty()) {
            std::println(stderr, "ariadne: unknown shortcut \"{}\" for \"{}\"", action.shortcut.toStdString(),
                action.name.toStdString());
            continue;
        }
        QAction* shortcut = makeAction(this, action.name, {keys});
        connect(shortcut, &QAction::triggered, this, [this, action] {
            if (!location::isLocal(m_location) || location::isTrash(m_location))
                return;
            const std::vector<ActionTarget> targets = targetsOf(selectedEntries());
            if (appliesTo(action, targets, QMimeDatabase()))
                runCustomAction(action, targets);
            else
                m_toast->showMessage(u"“%1” does not apply to what is selected."_s.arg(action.name));
        });
        m_customShortcuts.push_back(shortcut);
    }
}

void MainWindow::fillArchiveMenu(QMenu& menu, const QModelIndex& index)
{
    // Only what reads: opening copies, copying out, unpacking.
    if (index.isValid()) {
        const std::vector<FileEntry> entries = selectedEntries();
        menu.addAction(u"Open"_s, this, [this, index] { activate(index); });
        if (std::ranges::none_of(entries, &FileEntry::isDir)) {
            QMenu* openWithMenu = menu.addMenu(u"Open With"_s);
            const Applications applications = Applications::load();
            for (const DesktopApp* app : applications.forMimeType(entries.front().mimeType)) {
                openWithMenu->addAction(tde::theme::themeIcon({app->iconName, u"application-x-executable"_s}),
                    app->name, this, [this, entries, id = app->id] { openWith(entries, id); });
            }
            openWithMenu->setEnabled(!openWithMenu->isEmpty());
        }
        menu.addSeparator();
        menu.addAction(m_copyAction);
        menu.addAction(u"Extract To…"_s, this, [this, entries] { extractFromArchive(entries); });
        return;
    }
    menu.addAction(u"Extract All…"_s, this, [this] {
        if (const auto place = location::archivePlace(m_location))
            extractArchives({place->file}, true);
    });
    if (const auto place = location::archivePlace(m_location)) {
        const QString file = place->file;
        QMenu* openWithMenu = menu.addMenu(u"Open Archive With"_s);
        const QString mimeType = QMimeDatabase().mimeTypeForFile(file).name();
        const Applications applications = Applications::load();
        for (const DesktopApp* app : applications.forMimeType(mimeType)) {
            openWithMenu->addAction(tde::theme::themeIcon({app->iconName, u"application-x-executable"_s}), app->name,
                this, [this, file, id = app->id] {
                    const Applications apps = Applications::load();
                    if (const DesktopApp* chosen = apps.find(id))
                        if (const auto launched = Applications::launch(*chosen, {file}, tde::desktop().terminal);
                            !launched)
                            m_toast->showMessage(launched.error());
                });
        }
        openWithMenu->setEnabled(!openWithMenu->isEmpty());
    }
    menu.addSeparator();
    menu.addAction(m_showHiddenAction);
    menu.addAction(m_reloadAction);
}

void MainWindow::showContextMenu(const QPoint& globalPosition, const QModelIndex& index)
{
    QMenu menu(this);
    if (location::isArchive(m_location)) {
        fillArchiveMenu(menu, index);
        menu.exec(globalPosition);
        return;
    }

    if (index.isValid()) {
        const std::vector<FileEntry> entries = selectedEntries();
        const bool inTrash = location::isTrash(m_location);
        const Executable kind = entries.size() == 1 && !inTrash
            ? executableKind(entries.front().path, entries.front().mimeType)
            : Executable::No;
        if (kind == Executable::No) {
            menu.addAction(u"Open"_s, this, [this, index] { activate(index); });
        } else {
            const FileEntry entry = entries.front();
            const auto runIt = [this, entry](bool inTerminal) {
                if (const auto started = runExecutable(entry.path, inTerminal, tde::desktop().terminal); !started)
                    m_toast->showMessage(started.error());
            };
            menu.addAction(u"Run"_s, this, [runIt] { runIt(false); });
            menu.addAction(u"Run in Terminal"_s, this, [runIt] { runIt(true); });
            menu.addAction(u"Open"_s, this, [this, entry] { openInApplication(entry); });
        }

        if (!inTrash) {
            QMenu* openWithMenu = menu.addMenu(u"Open With"_s);
            const Applications applications = Applications::load();
            for (const DesktopApp* app : applications.forMimeType(entries.front().mimeType)) {
                openWithMenu->addAction(tde::theme::themeIcon({app->iconName, u"application-x-executable"_s}),
                    app->name, this, [this, entries, id = app->id] { openWith(entries, id); });
            }
            if (!openWithMenu->isEmpty())
                openWithMenu->addSeparator();
            openWithMenu->addAction(u"Other Application…"_s, this, [this, entries] { chooseApplication(entries); });
        }
        if (entries.size() == 1 && entries.front().isDir) {
            menu.addAction(u"Open in New Window"_s, this, [this, url = entries.front().url] { m_app.openWindow(url); });
            if (!inTrash)
                menu.addAction(
                    u"Open in Terminal"_s, this, [this, path = entries.front().path] { openTerminal(path); });
        }

        if (!inTrash && archives::isAvailable())
            menu.addAction(u"Compress…"_s, this, &MainWindow::compressSelection);
        const bool allArchives = !inTrash && archives::isAvailable()
            && std::ranges::all_of(
                entries, [](const FileEntry& e) { return !e.isDir && archives::isArchive(e.mimeType); });
        if (allArchives) {
            menu.addSeparator();
            menu.addAction(u"Extract Here"_s, this, [this] { extractSelection(false); });
            menu.addAction(u"Extract to…"_s, this, [this] { extractSelection(true); });
        }

        if (!inTrash)
            addCustomActions(menu, entries);

        menu.addSeparator();
        if (inTrash) {
            menu.addAction(u"Restore"_s, this, &MainWindow::restoreSelection);
        } else if (entries.size() == 1 && entries.front().isDir) {
            const QUrl url = entries.front().url;
            if (m_app.bookmarks().contains(url))
                menu.addAction(u"Remove from Bookmarks"_s, this, [this, url] { m_app.bookmarks().remove(url); });
            else
                menu.addAction(u"Add to Bookmarks"_s, this, [this, url] { m_app.bookmarks().add(url); });
        }
        if (!inTrash) {
            menu.addAction(m_cutAction);
            menu.addAction(m_copyAction);
            if (entries.size() == 1 && entries.front().isDir && clipboardFiles()) {
                menu.addAction(u"Paste Into Folder"_s, this, [this, path = entries.front().path] { paste(path); });
            }
        }
        if (!inTrash)
            menu.addAction(m_renameAction);
        menu.addAction(u"Copy Location"_s, this,
            [entries] { QGuiApplication::clipboard()->setText(pathsOf(entries).join(u'\n')); });
        menu.addSeparator();
        if (!inTrash)
            menu.addAction(m_trashAction);
        menu.addAction(m_deleteAction);
        menu.addSeparator();
        menu.addAction(m_propertiesAction);
    } else {
        menu.addAction(m_pasteAction);
        m_pasteAction->setEnabled(canCreateHere() && clipboardFiles());
        menu.addSeparator();
        menu.addAction(m_newFolderAction);
        QMenu* documents = menu.addMenu(u"New Document"_s);
        documents->setEnabled(canCreateHere());
        documents->addAction(tde::theme::themeIcon({u"text-x-generic"_s}), u"Empty Document"_s, this,
            [this] { createItem(NewItem::EmptyDocument); });
        const QList<QFileInfo> templates = documentTemplates();
        if (!templates.isEmpty())
            documents->addSeparator();
        const QMimeDatabase mimeDatabase;
        for (const QFileInfo& file : templates) {
            const QMimeType mime = mimeDatabase.mimeTypeForFile(file);
            documents->addAction(tde::theme::themeIcon({mime.iconName(), mime.genericIconName()}),
                file.completeBaseName(), this,
                [this, path = file.absoluteFilePath()] { createItem(NewItem::FromTemplate, path); });
        }
        menu.addSeparator();
        if (location::isLocal(m_location))
            menu.addAction(u"Open in Terminal"_s, this, [this] { openTerminal(location::localPath(m_location)); });
        if (location::isLocal(m_location) && !location::isTrash(m_location))
            addCustomActions(menu, {});
        menu.addAction(m_bookmarkAction);
        menu.addAction(m_newWindowAction);
        menu.addSeparator();
        menu.addAction(m_showHiddenAction);
        menu.addAction(m_reloadAction);
        if (location::isTrash(m_location)) {
            menu.addSeparator();
            menu.addAction(u"Empty Trash…"_s, this, &MainWindow::emptyTrash)->setEnabled(m_model->rowCount() > 0);
        }
        menu.addSeparator();
        menu.addAction(u"Properties"_s, this, [this] {
            FileEntry folder;
            folder.name = location::displayName(m_location);
            folder.path = location::localPath(m_location);
            folder.url = m_location;
            folder.isDir = true;
            folder.mimeType = u"inode/directory"_s;
            folder.mimeComment = QMimeDatabase().mimeTypeForName(folder.mimeType).comment();
            showPropertiesOf({folder});
        });
    }

    menu.exec(globalPosition);
}

bool MainWindow::refuseInArchive()
{
    const auto place = location::archivePlace(m_location);
    if (!place)
        return false;
    m_toast->showMessage(
        u"“%1” can only be read here; extract it to change what is in it."_s.arg(QFileInfo(place->file).fileName()));
    return true;
}

void MainWindow::trashSelection()
{
    if (refuseInArchive())
        return;
    // Things in the trash can only be deleted for good.
    if (location::isTrash(m_location)) {
        deleteSelection();
        return;
    }
    trashPaths(pathsOf(selectedEntries()));
}

void MainWindow::trashPaths(const QStringList& paths)
{
    if (paths.isEmpty())
        return;
    const QString what = describe(paths);
    auto trashed = std::make_shared<QList<fileops::Trashed>>();
    runFileOperation(
        [paths, trashed] {
            fileops::TrashResult result = fileops::moveToTrash(paths);
            *trashed = std::move(result.trashed);
            return result.failures;
        },
        u"%1 moved to Trash."_s.arg(what), u"move to Trash"_s,
        [this, trashed, what] {
            if (trashed->isEmpty())
                return false;
            m_app.pushUndo(u"Move to Trash"_s, [items = *trashed, what](MainWindow& window) {
                window.reportUndo(fileops::restoreTrashed(items), u"%1 restored."_s.arg(what), u"restore"_s);
            });
            return true;
        });
}

void MainWindow::deleteSelection()
{
    if (refuseInArchive())
        return;
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty())
        return;
    const QString what
        = entries.size() == 1 ? u"“%1”"_s.arg(entries.front().name) : u"%1 selected items"_s.arg(entries.size());
    if (!tde::Dialog::confirm(this, u"Delete Permanently"_s, u"Permanently delete %1?"_s.arg(what),
            u"Deleted items cannot be restored."_s, u"Delete"_s))
        return;
    const QString done = entries.size() == 1 ? u"“%1” deleted."_s.arg(entries.front().name)
                                             : u"%1 items deleted."_s.arg(entries.size());
    runFileOperation([paths = pathsOf(entries)] { return fileops::deletePermanently(paths); }, done, u"delete"_s);
}

QStringList MainWindow::pathsOf(const std::vector<FileEntry>& entries)
{
    QStringList paths;
    for (const FileEntry& entry : entries)
        paths << entry.path;
    return paths;
}

void MainWindow::runFileOperation(std::function<QList<fileops::Failure>()> job, const QString& doneMessage,
    const QString& verb, std::function<bool()> recordUndo)
{
    // Large folders take a while to delete; keep the window responsive meanwhile.
    auto* watcher = new QFutureWatcher<QList<fileops::Failure>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, doneMessage, verb, recordUndo] {
        watcher->deleteLater();
        const QList<fileops::Failure> failures = watcher->result();
        m_model->reload();
        const bool undoable = recordUndo && recordUndo();
        if (failures.isEmpty()) {
            showUndoableMessage(doneMessage, undoable);
            return;
        }
        const fileops::Failure& first = failures.first();
        QString message = u"Could not %1 “%2”: %3"_s.arg(verb, QFileInfo(first.path).fileName(), first.reason);
        if (failures.size() > 1)
            message += u" (and %1 more)"_s.arg(failures.size() - 1);
        m_toast->showMessage(message, 6000);
    });
    watcher->setFuture(QtConcurrent::run(std::move(job)));
}

void MainWindow::restoreSelection()
{
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty() || !location::isTrash(m_location))
        return;
    const QString done = entries.size() == 1 ? u"“%1” restored."_s.arg(entries.front().name)
                                             : u"%1 items restored."_s.arg(entries.size());
    runFileOperation([paths = pathsOf(entries)] { return fileops::restoreFromTrash(paths); }, done, u"restore"_s);
}

void MainWindow::emptyTrash()
{
    if (!tde::Dialog::confirm(this, u"Empty Trash"_s, u"Permanently delete everything in the Trash?"_s,
            u"The items cannot be restored afterwards."_s, u"Empty Trash"_s))
        return;
    runFileOperation([] { return fileops::emptyTrash(); }, u"The Trash is empty."_s, u"delete"_s);
}

void MainWindow::openTerminal(const QString& directory)
{
    if (const auto opened = terminal::openIn(directory, tde::desktop().terminal); !opened)
        m_toast->showMessage(opened.error(), 6000);
}

void MainWindow::openSearch(const QString& text)
{
    if (location::isArchive(m_location)) {
        m_searchAction->setChecked(false);
        m_toast->showMessage(u"Archives cannot be searched."_s);
        return;
    }
    m_locationStack->setCurrentWidget(m_searchEntry);
    m_searchAction->setChecked(true);
    m_searchEntry->setPlaceholderText(u"Search in “%1” and its folders"_s.arg(location::displayName(m_location)));
    m_searchEntry->setFocus();
    if (!text.isEmpty()) {
        m_searchEntry->setText(text);
        m_searchTimer.start();
    } else {
        m_searchEntry->selectAll();
    }
}

void MainWindow::closeSearch(bool showFolder)
{
    if (m_locationStack->currentWidget() != m_searchEntry)
        return;
    m_searchTimer.stop();
    m_locationStack->setCurrentWidget(m_pathBar);
    m_searchAction->setChecked(false);
    m_searchEntry->clear();
    if (showFolder && !m_model->searchQuery().isEmpty())
        m_model->setLocation(m_location);
    m_view->focusView();
}

void MainWindow::runSearch()
{
    const QString query = m_searchEntry->text().trimmed();
    if (query.isEmpty()) {
        if (!m_model->searchQuery().isEmpty())
            m_model->setLocation(m_location);
        return;
    }
    m_loadError.clear();
    m_model->setSearch(m_location, query, m_proxy->showHidden());
    updatePlaceholder();
}

void MainWindow::renameSelection()
{
    if (refuseInArchive())
        return;
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty() || location::isTrash(m_location))
        return;
    if (entries.size() > 1) {
        batchRename(pathsOf(entries));
        return;
    }
    const FileEntry& entry = entries.front();
    for (const QModelIndex& index : m_view->selectedRows()) {
        if (entryAt(index).path == entry.path)
            m_view->editName(index);
    }
}

void MainWindow::renameItem(const QString& path, const QString& requested)
{
    const QFileInfo info(path);
    const QString newName = requested.trimmed();
    if (newName == info.fileName())
        return;
    if (newName.isEmpty() || newName == u"." || newName == u".." || newName.contains(u'/')) {
        m_toast->showMessage(u"“%1” is not a valid name."_s.arg(newName));
        return;
    }
    const QString target = info.absoluteDir().filePath(newName);
    // Changing only the case of a name is fine even where names ignore case.
    const bool caseOnly = newName.compare(info.fileName(), Qt::CaseInsensitive) == 0;
    if (!caseOnly && (QFileInfo(target).exists() || QFileInfo(target).isSymLink())) {
        m_toast->showMessage(u"“%1” already exists."_s.arg(newName));
        return;
    }
    if (!QDir().rename(path, target)) {
        m_toast->showMessage(u"Could not rename “%1”."_s.arg(info.fileName()));
        return;
    }
    const QUrl url = location::fromLocalPath(path);
    if (info.isDir() && m_app.bookmarks().contains(url)) {
        m_app.bookmarks().remove(url);
        m_app.bookmarks().add(location::fromLocalPath(target));
    }
    m_app.pushUndo(u"Rename"_s, [from = target, to = path](MainWindow& window) {
        QList<fileops::Failure> failures;
        if (QFileInfo(to).exists() || !QDir().rename(from, to))
            failures << fileops::Failure {from, u"It could not get its old name back."_s};
        window.reportUndo(failures, u"Renamed back to “%1”."_s.arg(QFileInfo(to).fileName()), u"rename back"_s);
    });
    m_selectWhenCreated = newName;
    m_model->reload();
    showUndoableMessage(u"Renamed to “%1”."_s.arg(newName), true);
}

void MainWindow::batchRename(const QStringList& paths)
{
    const auto plans = askBatchRename(this, paths);
    if (!plans)
        return;
    const batchrename::Outcome outcome = batchrename::apply(*plans);
    if (!outcome.renamed.isEmpty()) {
        QList<std::pair<QString, QString>> back;
        for (const auto& [from, to] : outcome.renamed)
            back << std::pair {to, from};
        m_app.pushUndo(u"Rename"_s, [back](MainWindow& window) {
            window.reportUndo(batchrename::renameAll(back).failures,
                u"%1 items got their old names back."_s.arg(back.size()), u"rename back"_s);
        });
    }
    m_model->reload();
    if (!outcome.failures.isEmpty()) {
        const fileops::Failure& first = outcome.failures.first();
        m_toast->showMessage(u"Could not rename “%1”: %2"_s.arg(QFileInfo(first.path).fileName(), first.reason), 6000);
        return;
    }
    showUndoableMessage(u"Renamed %1 items."_s.arg(outcome.renamed.size()), !outcome.renamed.isEmpty());
}

void MainWindow::extractSelection(bool askForDestination)
{
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty())
        return;
    // Inside an archive: what is selected, out of it.
    if (location::isArchive(m_location)) {
        extractFromArchive(entries);
        return;
    }
    extractArchives(pathsOf(entries), askForDestination);
}

std::optional<QString> MainWindow::askExtractDestination(const QString& suggestion)
{
    const auto text = tde::Dialog::getText(this, u"Extract To"_s, u"Folder to extract into (created if missing)"_s,
        location::editableText(location::fromLocalPath(suggestion)), u"Extract"_s);
    if (!text)
        return std::nullopt;
    const auto url = location::fromUserInput(*text, location::fromLocalPath(suggestion));
    if (!url || !location::isLocal(*url) || !QDir().mkpath(location::localPath(*url))) {
        m_toast->showMessage(u"“%1” is not a folder that can be extracted into."_s.arg(*text));
        return std::nullopt;
    }
    return location::localPath(*url);
}

void MainWindow::extractFromArchive(const std::vector<FileEntry>& entries)
{
    const auto place = location::archivePlace(m_location);
    if (!place)
        return;
    const auto destination = askExtractDestination(QFileInfo(place->file).absolutePath());
    if (!destination)
        return;
    QStringList paths;
    for (const FileEntry& entry : entries) {
        if (const auto inside = location::archivePlace(entry.url))
            paths << inside->inside;
    }
    auto* watcher = new QFutureWatcher<std::expected<void, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, destination = *destination] {
        watcher->deleteLater();
        const auto result = watcher->result();
        m_toast->showMessage(
            result ? u"Extracted to “%1”."_s.arg(destination) : u"Could not extract: %1"_s.arg(result.error()),
            result ? 4000 : 7000);
    });
    watcher->setFuture(QtConcurrent::run(archives::extractPaths, place->file, paths, *destination));
}

void MainWindow::extractArchives(const QStringList& archivePaths, bool askForDestination)
{
    QString destination = location::isLocal(m_location) ? location::localPath(m_location)
                                                        : QFileInfo(archivePaths.value(0)).absolutePath();
    if (askForDestination) {
        const auto chosen = askExtractDestination(destination);
        if (!chosen)
            return;
        destination = *chosen;
    }
    if (!QFileInfo(destination).isWritable()) {
        m_toast->showMessage(u"You cannot create files in “%1”."_s.arg(destination));
        return;
    }

    m_toast->showMessage(archivePaths.size() == 1
            ? u"Extracting “%1”…"_s.arg(QFileInfo(archivePaths.front()).fileName())
            : u"Extracting %1 archives…"_s.arg(archivePaths.size()));
    using Results = QList<std::pair<QString, std::expected<QString, QString>>>;
    auto* watcher = new QFutureWatcher<Results>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, destination] {
        watcher->deleteLater();
        const Results results = watcher->result();
        m_model->reload();
        QStringList failures;
        QString extracted;
        for (const auto& [archive, result] : results) {
            if (result)
                extracted = *result;
            else
                failures << u"“%1”: %2"_s.arg(QFileInfo(archive).fileName(), result.error());
        }
        if (!failures.isEmpty()) {
            m_toast->showMessage(u"Could not extract %1"_s.arg(failures.join(u"; "_s)), 7000);
            return;
        }
        if (QDir::cleanPath(destination) == QDir::cleanPath(location::localPath(m_location)))
            m_selectWhenCreated = QFileInfo(extracted).fileName();
        m_toast->showMessage(u"Extracted to “%1”."_s.arg(extracted));
    });
    watcher->setFuture(QtConcurrent::run([archivePaths, destination] {
        Results results;
        for (const QString& path : archivePaths)
            results.append({path, archives::extract(path, destination)});
        return results;
    }));
}

void MainWindow::compressSelection()
{
    if (refuseInArchive())
        return;
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty())
        return;
    // Next to the (first) item, named after it, or "Archive" for several.
    const QString directory = QFileInfo(entries.front().path).absolutePath();
    const QString suggested = entries.size() == 1
        ? (entries.front().isDir ? entries.front().name : QFileInfo(entries.front().name).completeBaseName())
        : u"Archive"_s;
    const auto chosen = dialogs::askArchiveName(this, suggested, static_cast<int>(entries.size()));
    if (!chosen)
        return;
    if (!QFileInfo(directory).isWritable()) {
        m_toast->showMessage(u"You cannot create files in “%1”."_s.arg(directory));
        return;
    }
    const QString archive = QDir(directory).filePath(fileops::uniqueName(directory, *chosen));

    m_toast->showMessage(u"Compressing into “%1”…"_s.arg(QFileInfo(archive).fileName()));
    auto* watcher = new QFutureWatcher<std::expected<void, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, archive, directory] {
        watcher->deleteLater();
        const auto result = watcher->result();
        m_model->reload();
        if (!result) {
            m_toast->showMessage(u"Could not compress: %1"_s.arg(result.error()), 7000);
            return;
        }
        m_app.pushUndo(u"Compress"_s, [archive](MainWindow& window) {
            window.reportUndo(fileops::moveToTrash({archive}).failures,
                u"“%1” moved to Trash."_s.arg(QFileInfo(archive).fileName()), u"remove"_s);
        });
        if (QDir::cleanPath(directory) == QDir::cleanPath(location::localPath(m_location)))
            m_selectWhenCreated = QFileInfo(archive).fileName();
        showUndoableMessage(u"Created “%1”."_s.arg(QFileInfo(archive).fileName()), true);
    });
    watcher->setFuture(
        QtConcurrent::run([paths = pathsOf(entries), archive] { return archives::compress(paths, archive); }));
}

void MainWindow::openWith(const std::vector<FileEntry>& entries, const QString& appId)
{
    const Applications applications = Applications::load();
    const DesktopApp* app = applications.find(appId);
    if (!app || entries.empty())
        return;
    const auto launch = [this, app = *app](const QStringList& paths) {
        if (const auto launched = Applications::launch(app, paths, tde::desktop().terminal); !launched)
            m_toast->showMessage(launched.error());
    };
    if (location::isArchive(entries.front().url))
        extractCopies(entries, launch);
    else
        launch(pathsOf(entries));
}

void MainWindow::extractCopies(const std::vector<FileEntry>& entries, std::function<void(const QStringList&)> then)
{
    // Grouped by the folder they are in, each group in a folder of its own: names may repeat.
    QMap<QString, QStringList> groups; // archive folder location → paths inside the archive
    QString archive;
    for (const FileEntry& entry : entries) {
        const auto place = location::archivePlace(entry.url);
        if (!place)
            continue;
        archive = place->file;
        groups[location::parent(entry.url)->toString()] << place->inside;
    }
    if (groups.isEmpty())
        return;
    const QString staging = archives::stagingFolder();
    auto* watcher = new QFutureWatcher<std::expected<QStringList, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, then = std::move(then)] {
        watcher->deleteLater();
        const auto result = watcher->result();
        if (!result)
            m_toast->showMessage(u"Could not unpack: %1"_s.arg(result.error()), 7000);
        else
            then(*result);
    });
    watcher->setFuture(QtConcurrent::run([archive, groups, staging]() -> std::expected<QStringList, QString> {
        QStringList extracted;
        int group = 0;
        for (const QStringList& paths : groups) {
            const QString folder = u"%1/%2"_s.arg(staging).arg(group++);
            if (const auto done = archives::extractPaths(archive, paths, folder); !done)
                return std::unexpected(done.error());
            for (const QString& path : paths)
                extracted << folder + u'/' + path.section(u'/', -1);
        }
        return extracted;
    }));
}

void MainWindow::chooseApplication(const std::vector<FileEntry>& entries)
{
    if (entries.empty())
        return;
    const FileEntry& first = entries.front();
    const Applications applications = Applications::load();
    const auto choice = ariadne::chooseApplication(this, applications, first.mimeType, first.mimeComment,
        entries.size() == 1 ? first.name : u"%1 items"_s.arg(entries.size()));
    if (!choice)
        return;
    if (choice->makeDefault) {
        if (const auto saved = Applications::setDefault(first.mimeType, choice->appId); !saved)
            m_toast->showMessage(u"Could not save the default application: %1"_s.arg(saved.error()));
    }
    openWith(entries, choice->appId);
}

void MainWindow::showPropertiesOfPaths(const QStringList& paths)
{
    const QMimeDatabase mimeDatabase;
    std::vector<FileEntry> entries;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.exists() || info.isSymLink())
            entries.push_back(describeEntry(info, location::fromLocalPath(path), false, mimeDatabase));
    }
    showPropertiesOf(entries);
}

void MainWindow::showPropertiesOf(const std::vector<FileEntry>& entries)
{
    if (entries.empty())
        return;
    QIcon icon;
    if (const int row = m_model->rowOf(entries.front().name);
        row >= 0 && m_model->entry(row).path == entries.front().path)
        icon = m_model->index(row, 0).data(Qt::DecorationRole).value<QIcon>();
    if (icon.isNull())
        icon = tde::theme::themeIcon({location::directoryIconName(entries.front().path), u"folder"_s});
    showProperties(this, entries, icon);
}

void MainWindow::updateStatus()
{
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty()) {
        m_view->setStatusText({});
        return;
    }
    const auto size = [](qint64 bytes) { return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat); };
    if (entries.size() == 1) {
        const FileEntry& entry = entries.front();
        const QString detail = !entry.isDir ? size(entry.size)
            : entry.childCount < 0          ? QString()
            : entry.childCount == 1         ? u"containing 1 item"_s
                                            : u"containing %1 items"_s.arg(entry.childCount);
        QString text
            = detail.isEmpty() ? u"“%1” selected"_s.arg(entry.name) : u"“%1” selected (%2)"_s.arg(entry.name, detail);
        // Search results come from all over; say where this one is.
        if (!m_model->searchQuery().isEmpty()) {
            if (const auto folder = location::parent(entry.url))
                text += u" in %1"_s.arg(location::editableText(*folder));
        }
        m_view->setStatusText(text);
        return;
    }
    qint64 bytes = 0;
    int folders = 0;
    for (const FileEntry& entry : entries) {
        bytes += entry.isDir ? 0 : entry.size;
        folders += entry.isDir ? 1 : 0;
    }
    QString text = u"%1 items selected"_s.arg(entries.size());
    if (folders == 0)
        text += u" (%1)"_s.arg(size(bytes));
    else if (folders < static_cast<int>(entries.size()))
        text += u" (%1 folders, %2 in files)"_s.arg(folders).arg(size(bytes));
    m_view->setStatusText(text);
}

void MainWindow::updateTrashBar()
{
    const auto place = location::archivePlace(m_location);
    m_archiveBar->setVisible(place.has_value());
    if (place)
        m_archiveLabel->setText(
            u"Inside the archive “%1”, which can only be read here."_s.arg(QFileInfo(place->file).fileName()));

    const bool inTrash = location::isTrash(m_location);
    m_trashBar->setVisible(inTrash);
    if (!inTrash)
        return;
    m_restoreButton->setEnabled(!selectedEntries().empty());
    m_emptyTrashButton->setEnabled(m_model->rowCount() > 0);
}

QString MainWindow::describe(const QStringList& paths)
{
    return paths.size() == 1 ? u"“%1”"_s.arg(QFileInfo(paths.first()).fileName()) : u"%1 items"_s.arg(paths.size());
}

void MainWindow::showUndoableMessage(const QString& text, bool undoable)
{
    if (undoable)
        m_toast->showMessage(text, 7000, u"Undo"_s, [this] { m_app.undo(*this); });
    else
        m_toast->showMessage(text);
}

void MainWindow::reportUndo(const QList<fileops::Failure>& failures, const QString& doneMessage, const QString& verb)
{
    m_model->reload();
    if (failures.isEmpty()) {
        m_toast->showMessage(doneMessage);
        return;
    }
    const fileops::Failure& first = failures.first();
    m_toast->showMessage(u"Could not %1 “%2”: %3"_s.arg(verb, QFileInfo(first.path).fileName(), first.reason), 6000);
}

void MainWindow::updateUndo()
{
    m_undoAction->setEnabled(m_app.canUndo());
    m_undoAction->setText(m_app.canUndo() ? u"Undo %1"_s.arg(m_app.undoDescription()) : u"Undo"_s);
}

void MainWindow::startTransfer(TransferKind kind, QList<TransferItem> items, bool undoable)
{
    if (items.isEmpty())
        return;
    QStringList sources;
    for (const TransferItem& item : std::as_const(items))
        sources << item.source;
    const QString what = describe(sources);
    const QString directory = QFileInfo(items.first().destination).absolutePath();
    const QString where = location::displayName(location::fromLocalPath(directory));
    const bool copy = kind == TransferKind::Copy;
    const QString description
        = copy ? u"Copying %1 to “%2”"_s.arg(what, where) : u"Moving %1 to “%2”"_s.arg(what, where);

    QPointer<MainWindow> self(this);
    Application& app = m_app;
    m_app.jobs().start(
        kind, std::move(items), description,
        // Conflicts are asked in this window, or on their own if it was closed meanwhile.
        [self](const Conflict& conflict) { return dialogs::askConflict(self, conflict); },
        [self, &app, kind, copy, what, where, directory, undoable](const TransferResult& result) {
            const bool recorded = undoable && !result.completed.isEmpty();
            if (recorded) {
                app.pushUndo(copy ? u"Copy"_s : u"Move"_s, [kind, completed = result.completed](MainWindow& window) {
                    if (kind == TransferKind::Copy) {
                        QStringList copies;
                        for (const TransferItem& item : completed)
                            copies << item.destination;
                        window.runFileOperation([copies] { return fileops::moveToTrash(copies).failures; },
                            u"The copies were moved to Trash."_s, u"remove the copy of"_s);
                        return;
                    }
                    QList<TransferItem> back;
                    for (const TransferItem& item : completed)
                        back << TransferItem {item.destination, item.source};
                    window.startTransfer(TransferKind::Move, back, false);
                });
            }
            if (!self)
                return;
            self->m_model->reload();
            if (!result.completed.isEmpty()
                && QDir::cleanPath(directory) == QDir::cleanPath(location::localPath(self->m_location)))
                self->m_selectWhenCreated = QFileInfo(result.completed.first().destination).fileName();

            if (!result.failures.isEmpty()) {
                const fileops::Failure& first = result.failures.first();
                QString message = u"Could not %1 “%2”: %3"_s.arg(
                    copy ? u"copy"_s : u"move"_s, QFileInfo(first.path).fileName(), first.reason);
                if (result.failures.size() > 1)
                    message += u" (and %1 more)"_s.arg(result.failures.size() - 1);
                self->m_toast->showMessage(message, 7000);
            } else if (result.cancelled) {
                self->showUndoableMessage(copy ? u"Copying was cancelled."_s : u"Moving was cancelled."_s, recorded);
            } else if (!result.completed.isEmpty()) {
                self->showUndoableMessage(
                    copy ? u"Copied %1 to “%2”."_s.arg(what, where) : u"Moved %1 to “%2”."_s.arg(what, where),
                    recorded);
            }
        });
}

void MainWindow::drop(const QStringList& paths, const QString& directory, Qt::DropAction action)
{
    if (directory.isEmpty())
        trashPaths(paths);
    else
        startTransfer(
            action == Qt::CopyAction ? TransferKind::Copy : TransferKind::Move, Transfer::into(paths, directory));
}

void MainWindow::putOnClipboard(bool cut)
{
    const std::vector<FileEntry> entries = selectedEntries();
    if (entries.empty() || location::isTrash(m_location))
        return;
    // From an archive, copies are unpacked first: the clipboard holds files, for any program.
    if (location::isArchive(m_location)) {
        if (cut) {
            refuseInArchive();
            return;
        }
        m_toast->showMessage(u"Unpacking %1…"_s.arg(
            entries.size() == 1 ? u"“%1”"_s.arg(entries.front().name) : u"%1 items"_s.arg(entries.size())));
        extractCopies(entries, [this](const QStringList& paths) {
            QGuiApplication::clipboard()->setMimeData(clipboard::encode({paths, false}).release());
            m_toast->showMessage(u"%1 copied."_s.arg(describe(paths)));
        });
        return;
    }
    QGuiApplication::clipboard()->setMimeData(clipboard::encode({pathsOf(entries), cut}).release());
    const QString what = describe(pathsOf(entries));
    m_toast->showMessage(cut ? u"%1 will be moved when pasted."_s.arg(what) : u"%1 copied."_s.arg(what));
}

std::optional<clipboard::Files> MainWindow::clipboardFiles()
{
    return clipboard::decode(QGuiApplication::clipboard()->mimeData());
}

void MainWindow::paste(const QString& directory)
{
    const QString target = directory.isEmpty() ? location::localPath(m_location) : directory;
    if (directory.isEmpty() && !canCreateHere()) {
        m_toast->showMessage(u"Nothing can be pasted here."_s);
        return;
    }
    const auto files = clipboardFiles();
    if (!files) {
        m_toast->showMessage(u"There are no files on the clipboard."_s);
        return;
    }
    // Cut files are moved once; after that the clipboard holds nothing to paste again.
    if (files->cut)
        QGuiApplication::clipboard()->clear();
    startTransfer(files->cut ? TransferKind::Move : TransferKind::Copy, Transfer::into(files->paths, target));
}

bool MainWindow::canCreateHere() const
{
    if (!location::isLocal(m_location))
        return false;
    const QFileInfo directory(location::localPath(m_location));
    return directory.isDir() && directory.isWritable();
}

QList<QFileInfo> MainWindow::documentTemplates()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::TemplatesLocation);
    if (directory.isEmpty() || QDir::cleanPath(directory) == QDir::cleanPath(QDir::homePath()))
        return {};
    return QDir(directory).entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
}

void MainWindow::createItem(NewItem kind, const QString& templatePath)
{
    if (!canCreateHere())
        return;
    const QString directory = location::localPath(m_location);

    const bool folder = kind == NewItem::Folder;
    QString suggested = folder           ? u"New Folder"_s
        : kind == NewItem::EmptyDocument ? u"New Document.txt"_s
                                         : QFileInfo(templatePath).fileName();
    suggested = fileops::uniqueName(directory, suggested);
    // Preselect the name without its extension, so typing keeps the type.
    const qsizetype stem = folder ? -1 : QFileInfo(suggested).completeBaseName().size();
    const auto name = tde::Dialog::getText(this, folder ? u"New Folder"_s : u"New Document"_s,
        folder ? u"Folder name"_s : u"File name"_s, suggested, u"Create"_s, stem > 0 ? stem : -1);
    if (!name)
        return;

    const QString trimmed = name->trimmed();
    if (trimmed.isEmpty() || trimmed == u"." || trimmed == u".." || trimmed.contains(u'/')) {
        m_toast->showMessage(u"“%1” is not a valid name."_s.arg(trimmed));
        return;
    }
    const QString path = QDir(directory).filePath(trimmed);
    if (const QFileInfo existing(path); existing.exists() || existing.isSymLink()) {
        m_toast->showMessage(u"“%1” already exists."_s.arg(trimmed));
        return;
    }

    bool created = false;
    if (folder) {
        created = QDir().mkdir(path);
    } else if (kind == NewItem::FromTemplate) {
        created = QFile::copy(templatePath, path);
    } else {
        QFile file(path);
        created = file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
    }
    if (!created) {
        m_toast->showMessage(u"Could not create “%1”."_s.arg(trimmed));
        return;
    }

    m_app.pushUndo(folder ? u"New Folder"_s : u"New Document"_s, [path](MainWindow& window) {
        window.reportUndo(fileops::moveToTrash({path}).failures,
            u"“%1” moved to Trash."_s.arg(QFileInfo(path).fileName()), u"remove"_s);
    });
    m_selectWhenCreated = trimmed;
    m_model->reload();
}

void MainWindow::showMounted(const std::expected<QString, QString>& result, bool newWindow)
{
    if (!result) {
        // An empty error means the user cancelled.
        if (!result.error().isEmpty())
            m_toast->showMessage(u"Could not mount the volume: %1"_s.arg(result.error()));
        return;
    }
    const QUrl url = location::fromLocalPath(*result);
    newWindow ? static_cast<void>(m_app.openWindow(url)) : navigate(url);
}

void MainWindow::mountDevice(const QString& id, bool newWindow)
{
    const std::optional<Device> device = m_app.devices().find(id);
    if (device && device->locked) {
        unlockDevice(id, newWindow);
        return;
    }
    const QString name = device ? device->label : QString();
    m_app.devices().mount(
        id,
        [self = QPointer(this), newWindow](std::expected<QString, QString> result) {
            if (self)
                self->showMounted(result, newWindow);
        },
        [self = QPointer(this), name](const gvfs::Prompt& prompt) -> std::optional<QString> {
            return self ? dialogs::answerPrompt(self, name, prompt) : std::nullopt;
        });
}

void MainWindow::unlockDevice(const QString& id, bool newWindow, const QString& error)
{
    const std::optional<Device> device = m_app.devices().find(id);
    if (!device)
        return;
    const std::optional<QString> passphrase = dialogs::askPassphrase(this, device->label, error);
    if (!passphrase)
        return;
    m_app.devices().unlock(
        id, *passphrase, [self = QPointer(this), id, newWindow](std::expected<QString, QString> result) {
            if (!self)
                return;
            // Most likely a mistyped passphrase: ask again, saying what went wrong.
            if (!result
                && self->m_app.devices().find(id).transform([](const Device& d) { return d.locked; }).value_or(false)) {
                self->unlockDevice(id, newWindow, result.error());
                return;
            }
            self->showMounted(result, newWindow);
        });
}

void MainWindow::makeDefault()
{
    if (!tde::Dialog::confirm(this, u"Make Default File Manager"_s, u"Use Ariadne as your file manager?"_s,
            u"Ariadne will open folders, show files when other applications ask (such as “Show in "
            u"Folder”), and start with your session so it is always ready. Open Nautilus windows "
            u"will close."_s,
            u"Make Default"_s, false))
        return;
    if (const auto made = defaults::makeDefault(QCoreApplication::applicationFilePath()); !made) {
        m_toast->showMessage(made.error());
        return;
    }
    FileManagerService::takeOverFromNautilus();
    m_toast->showMessage(u"Ariadne is now your file manager."_s);
}

void MainWindow::connectToServer()
{
    const std::optional<QString> address = tde::Dialog::getText(this, u"Connect to Server"_s,
        u"Server address, such as smb://server/share or sftp://host/"_s, {}, u"Connect"_s);
    if (!address || address->trimmed().isEmpty())
        return;
    const QString uri = address->trimmed();
    m_app.devices().connectTo(
        uri,
        [self = QPointer(this), uri](const gvfs::Prompt& prompt) -> std::optional<QString> {
            return self ? dialogs::answerPrompt(self, uri, prompt) : std::nullopt;
        },
        [self = QPointer(this)](std::expected<QString, QString> result) {
            if (self)
                self->showMounted(result, false);
        });
}

void MainWindow::unmountDevice(const QString& id, bool eject)
{
    const std::optional<Device> device = m_app.devices().find(id);
    if (!device)
        return;

    const QString label = device->label;
    if (!device->mountPoint.isEmpty()) {
        const QUrl mount = location::fromLocalPath(device->mountPoint);
        if (m_location == mount || location::isAncestorOf(mount, m_location))
            navigate(location::home());
    }

    auto done = [self = QPointer(this), label, eject](std::expected<void, QString> result) {
        if (!self)
            return;
        if (!result)
            self->m_toast->showMessage(
                u"Could not %1 “%2”: %3"_s.arg(eject ? u"eject"_s : u"unmount"_s, label, result.error()));
        else if (eject)
            self->m_toast->showMessage(u"“%1” can be safely removed."_s.arg(label));
    };
    eject ? m_app.devices().eject(id, std::move(done)) : m_app.devices().unmount(id, std::move(done));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_searchEntry && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape) {
            closeSearch(true);
            return true;
        }
        if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter) {
            // On to the results, starting with the first.
            m_searchTimer.stop();
            if (m_model->searchQuery() != m_searchEntry->text().trimmed())
                runSearch();
            m_view->focusView();
            if (m_proxy->rowCount() > 0)
                m_view->selectRow(m_proxy->index(0, 0));
            return true;
        }
    }
    if (watched == windowHandle() && event->type() == QEvent::MouseButtonPress) {
        const auto button = static_cast<QMouseEvent*>(event)->button();
        if (button == Qt::BackButton) {
            goBack();
            return true;
        }
        if (button == Qt::ForwardButton) {
            goForward();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        const int margin = (isMaximized() || isFullScreen()) ? 0 : 1;
        layout()->setContentsMargins(margin, margin, margin, margin);
        if (!isMinimized())
            m_app.updateConfig([&](Config& config) { config.window.maximized = isMaximized(); });
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QWidget::closeEvent(event);
    if (event->isAccepted())
        emit closed();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Mouse back/forward buttons arrive at the native window, which exists from now on.
    if (QWindow* handle = windowHandle(); handle && !m_handleWatched) {
        handle->installEventFilter(this);
        m_handleWatched = true;
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (isVisible() && !isMaximized() && !isFullScreen() && !isMinimized()) {
        m_app.updateConfig([&](Config& config) {
            config.window.width = width();
            config.window.height = height();
        });
    }
}

} // namespace ariadne
