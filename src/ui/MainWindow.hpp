#pragma once

#include "core/ClipboardFormat.hpp"
#include "core/Config.hpp"
#include "core/DirectoryListing.hpp"
#include "core/FileOperations.hpp"
#include "core/Transfer.hpp"

#include <QFileInfo>
#include <QList>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include <expected>
#include <functional>
#include <optional>
#include <vector>

class QAction;
class QActionGroup;
class QLabel;
class QLineEdit;
class QMenu;
class QStackedWidget;
class QPushButton;
class QModelIndex;
class QToolButton;

namespace tde {
class Toast;
} // namespace tde

namespace ariadne {

class Application;
class DirectoryModel;
class FileSortProxy;
class FileView;
class PathBar;
class Sidebar;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    MainWindow(Application& app, const QUrl& location, const QString& selectName = {}, QWidget* parent = nullptr);

    void navigate(const QUrl& url);
    void showPropertiesOfPaths(const QStringList& paths);
    // Takes changes to the desktop config (window buttons, theme) and to Ariadne's own.
    void applyDesktopConfig();
    void applyConfig(const Config& config);

signals:
    // The window was closed; its owner deletes it.
    void closed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    QWidget* createSidebarColumn();
    QWidget* createContentColumn();
    QMenu* createAppMenu();
    QMenu* createViewMenu();

    void setLocation(const QUrl& url);
    void goBack();
    void goForward();
    void goUp();
    void updateNavigation();
    void onLoaded();
    void updatePlaceholder();

    // Changes made by the user (`remember`) are remembered for the folder shown.
    void setViewMode(ViewMode mode, bool remember = true);
    void setSort(SortKey key, bool descending, bool remember = true);
    FolderView currentFolderView() const;
    void applyFolderView(const FolderView& view);
    void rememberFolderView();
    void onHeaderClicked(int column);
    void onPathEntered(const QString& text);

    FileEntry entryAt(const QModelIndex& proxyIndex) const;
    std::vector<FileEntry> selectedEntries() const;
    void activate(const QModelIndex& index);
    // Runs programs, asks about scripts, and opens everything else in its application.
    void openFile(const FileEntry& entry);
    void openInApplication(const FileEntry& entry);
    void showContextMenu(const QPoint& globalPosition, const QModelIndex& index);
    void fillArchiveMenu(QMenu& menu, const QModelIndex& index);

    void trashSelection();
    // Asks first, since deleted files are gone for good.
    void deleteSelection();
    // Runs `job` on a worker thread, then refreshes and reports how it went.
    // `recordUndo` runs afterwards, on this thread; it returns whether it added an undo step.
    void runFileOperation(std::function<QList<fileops::Failure>()> job, const QString& doneMessage, const QString& verb,
        std::function<bool()> recordUndo = {});
    void trashPaths(const QStringList& paths);
    // Copies or moves files, in the background with progress.
    void startTransfer(TransferKind kind, QList<TransferItem> items, bool undoable = true);
    void drop(const QStringList& paths, const QString& directory, Qt::DropAction action);
    void putOnClipboard(bool cut);
    // Pastes into `directory`, or the folder shown when empty.
    void paste(const QString& directory);
    static std::optional<clipboard::Files> clipboardFiles();
    void updateUndo();
    void showUndoableMessage(const QString& text, bool undoable);
    void reportUndo(const QList<fileops::Failure>& failures, const QString& doneMessage, const QString& verb);
    static QString describe(const QStringList& paths);
    static QStringList pathsOf(const std::vector<FileEntry>& entries);
    void restoreSelection();
    void emptyTrash();
    void renameSelection();
    // Renames after editing in place, with checks and undo.
    void renameItem(const QString& path, const QString& newName);
    void batchRename(const QStringList& paths);
    // The search entry takes the place of the path bar; results replace the folder's files.
    void openSearch(const QString& text);
    void openTerminal(const QString& directory);
    void compressSelection();
    void closeSearch(bool showFolder);
    void runSearch();
    void extractSelection(bool askForDestination);
    // Unpacks whole archives, here or where the user says.
    void extractArchives(const QStringList& archivePaths, bool askForDestination);
    // Unpacks what is selected inside the archive shown, where the user says.
    void extractFromArchive(const std::vector<FileEntry>& entries);
    std::optional<QString> askExtractDestination(const QString& suggestion);
    // Unpacks copies of entries inside an archive to a staging folder, then hands their paths on.
    void extractCopies(const std::vector<FileEntry>& entries, std::function<void(const QStringList&)> then);
    // Inside an archive, says it can only be read, and returns true.
    bool refuseInArchive();
    // Where activating `entry` goes: into a folder, or an archive opened as one.
    std::optional<QUrl> folderOf(const FileEntry& entry) const;
    void openWith(const std::vector<FileEntry>& entries, const QString& appId);
    void chooseApplication(const std::vector<FileEntry>& entries);
    void showPropertiesOf(const std::vector<FileEntry>& entries);
    void updateStatus();
    void updateTrashBar();

    enum class NewItem { Folder, EmptyDocument, FromTemplate };
    bool canCreateHere() const;
    // Files in the XDG templates folder (~/Templates), offered under "New Document".
    static QList<QFileInfo> documentTemplates();
    // Asks for a name, then creates the folder or document in the current folder.
    void createItem(NewItem kind, const QString& templatePath = {});

    void placeWindowButtons();
    // The user's own actions that apply to `entries` (empty: the folder shown), at the end of `menu`.
    void addCustomActions(QMenu& menu, const std::vector<FileEntry>& entries);
    void runCustomAction(const CustomAction& action, const std::vector<ActionTarget>& targets);
    void setupCustomShortcuts();
    void mountDevice(const QString& id, bool newWindow);
    void unlockDevice(const QString& id, bool newWindow, const QString& error = {});
    // Mounts a network location typed in by the user.
    void connectToServer();
    void makeDefault();
    void showMounted(const std::expected<QString, QString>& result, bool newWindow);
    void unmountDevice(const QString& id, bool eject);

    Application& m_app;
    DirectoryModel* m_model;
    FileSortProxy* m_proxy;
    FileView* m_view = nullptr;
    PathBar* m_pathBar = nullptr;
    Sidebar* m_sidebar = nullptr;
    tde::Toast* m_toast = nullptr;
    QToolButton* m_viewButton = nullptr;
    std::vector<QAction*> m_customShortcuts;
    QWidget* m_leftButtonSlot = nullptr;
    QWidget* m_rightButtonSlot = nullptr;

    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_upAction = nullptr;
    QAction* m_homeAction = nullptr;
    QAction* m_editLocationAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_showHiddenAction = nullptr;
    QAction* m_gridAction = nullptr;
    QAction* m_listAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_zoomResetAction = nullptr;
    QAction* m_bookmarkAction = nullptr;
    QAction* m_newWindowAction = nullptr;
    QAction* m_newFolderAction = nullptr;
    QAction* m_trashAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_renameAction = nullptr;
    QAction* m_propertiesAction = nullptr;
    QAction* m_searchAction = nullptr;
    QLineEdit* m_searchEntry = nullptr;
    QStackedWidget* m_locationStack = nullptr;
    QTimer m_searchTimer;
    QAction* m_cutAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QWidget* m_trashBar = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QPushButton* m_emptyTrashButton = nullptr;
    QWidget* m_archiveBar = nullptr;
    QLabel* m_archiveLabel = nullptr;
    QAction* m_closeAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_foldersFirstAction = nullptr;
    QAction* m_caseSensitiveAction = nullptr;
    QActionGroup* m_sortGroup = nullptr;

    QUrl m_location;
    QList<QUrl> m_backStack;
    QList<QUrl> m_forwardStack;
    QString m_selectAfterLoad;
    QString m_loadError;
    QString m_selectWhenCreated;
    bool m_handleWatched = false;
};

} // namespace ariadne
