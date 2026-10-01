#pragma once

#include "Jobs.hpp"
#include "MainWindow.hpp"
#include "core/Bookmarks.hpp"
#include "core/Config.hpp"
#include "core/DeviceMonitor.hpp"

#include <tde/ConfigWatcher.hpp>

#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <vector>

namespace ariadne {

// State shared by all windows.
class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(Config config, QString statePath = ariadne::statePath(), QObject* parent = nullptr);
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ~Application() override;

    // The settings new windows start with: the config, updated by every change made in any window.
    const Config& config() const { return m_config; }
    // Applies `change` to the settings and saves them shortly after.
    void updateConfig(const std::function<void(Config&)>& change);
    // Applies edits to the desktop config and Ariadne's config to all windows as they are saved.
    void watchConfig(const QString& desktopConfigPath, const QString& configPath);

    Bookmarks& bookmarks() { return m_bookmarks; }
    DeviceMonitor& devices() { return m_devices; }
    JobManager& jobs() { return m_jobs; }

    // How `location` is shown: as remembered for it, or else the default.
    FolderView folderView(const QUrl& location) const;
    void rememberFolderView(const QUrl& location, const FolderView& view);
    void forgetFolderView(const QUrl& location);

    // Undo history, shared by all windows. `undo` runs in the window it was asked from.
    using UndoAction = std::function<void(MainWindow& window)>;
    void pushUndo(const QString& description, UndoAction undo);
    bool canUndo() const { return !m_undo.empty(); }
    QString undoDescription() const { return m_undo.empty() ? QString() : m_undo.back().description; }
    void undo(MainWindow& window);

    // Opens a window showing `location`, selecting the entry `selectName` once it is listed.
    // The window is owned here, until it is closed.
    MainWindow* openWindow(const QUrl& location, const QString& selectName = {});
    bool hasWindows() const { return !m_windows.empty(); }

signals:
    void undoChanged();

private:
    struct UndoStep {
        QString description;
        UndoAction undo;
    };

    void saveState();
    void reloadDesktopConfig(const QString& path);
    void applyDesktopConfig();
    void reloadConfig();

    Config m_config;
    QString m_statePath;
    QString m_configPath;
    std::unique_ptr<tde::ConfigWatcher> m_configWatcher;
    QTimer m_saveTimer;
    FolderSettings m_folders;
    QTimer m_foldersSaveTimer;
    Bookmarks m_bookmarks;
    DeviceMonitor m_devices;
    JobManager m_jobs;
    std::vector<UndoStep> m_undo;
    // Last, so the windows are destroyed before what they use.
    std::vector<std::unique_ptr<MainWindow>> m_windows;
};

} // namespace ariadne
