#include "Application.hpp"

#include "MainWindow.hpp"
#include "core/Location.hpp"

#include <tde/Theme.hpp>

#include <QApplication>
#include <QFileInfo>
#include <QStyleHints>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace ariadne {

Application::Application(Config config, QString statePath, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_statePath(std::move(statePath))
    , m_folders(QFileInfo(m_statePath).absolutePath() + u"/folders.lua"_s) // next to the state
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Application::saveState);
    m_foldersSaveTimer.setSingleShot(true);
    m_foldersSaveTimer.setInterval(500);
    connect(&m_foldersSaveTimer, &QTimer::timeout, this, [this] { m_folders.save(); });
}

Application::~Application()
{
    if (m_saveTimer.isActive())
        saveState();
    if (m_foldersSaveTimer.isActive())
        m_folders.save();
    // The windows go first (they are the last member), as they use the bookmarks and devices.
}

void Application::updateConfig(const std::function<void(Config&)>& change)
{
    const Config previous = m_config;
    change(m_config);
    if (m_config != previous)
        m_saveTimer.start();
}

void Application::saveState()
{
    m_saveTimer.stop();
    if (!ariadne::saveState(m_config, m_statePath))
        std::println(stderr, "ariadne: could not save settings to {}", m_statePath.toStdString());
}

void Application::watchConfig(const QString& desktopConfigPath, const QString& configPath)
{
    m_configPath = configPath;
    m_configWatcher = std::make_unique<tde::ConfigWatcher>(QStringList {desktopConfigPath, configPath});
    connect(m_configWatcher.get(), &tde::ConfigWatcher::changed, this, [this, desktopConfigPath](const QString& path) {
        if (path == desktopConfigPath)
            reloadDesktopConfig(path);
        else
            reloadConfig();
    });
    // The "system" theme follows the desktop between light and dark.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (tde::desktop().appearance.theme == u"system")
            applyDesktopConfig();
    });
}

void Application::reloadDesktopConfig(const QString& path)
{
    tde::DesktopConfig fresh = tde::loadDesktopConfig(path);
    if (fresh == tde::desktop())
        return;
    tde::setDesktop(std::move(fresh));
    applyDesktopConfig();
}

void Application::applyDesktopConfig()
{
    tde::theme::apply(*qApp, tde::desktop().appearance);
    for (const auto& window : m_windows)
        window->applyDesktopConfig();
}

void Application::reloadConfig()
{
    Config fresh = loadConfig(m_configPath, m_statePath);
    // Window sizes are what the windows were last left at; the config only starts them off.
    fresh.window = m_config.window;
    if (fresh == m_config)
        return;
    m_config = std::move(fresh);
    for (const auto& window : m_windows)
        window->applyConfig(m_config);
}

namespace {

// Folders are remembered by path; other places (the trash) by their URL.
QString folderKey(const QUrl& location)
{
    return location::isLocal(location) ? location::localPath(location) : location.toString();
}

} // namespace

FolderView Application::folderView(const QUrl& location) const
{
    const Config::View& view = m_config.view;
    return m_folders.find(folderKey(location)).value_or(FolderView {view.mode, view.sortKey, view.sortDescending});
}

void Application::rememberFolderView(const QUrl& location, const FolderView& view)
{
    const Config::View& defaults = m_config.view;
    // A folder shown just like the default needs no entry of its own.
    if (view == FolderView {defaults.mode, defaults.sortKey, defaults.sortDescending})
        m_folders.remove(folderKey(location));
    else
        m_folders.set(folderKey(location), view);
    m_foldersSaveTimer.start();
}

void Application::forgetFolderView(const QUrl& location)
{
    m_folders.remove(folderKey(location));
    m_foldersSaveTimer.start();
}

void Application::pushUndo(const QString& description, UndoAction undo)
{
    constexpr std::size_t Limit = 30;
    m_undo.push_back({description, std::move(undo)});
    if (m_undo.size() > Limit)
        m_undo.erase(m_undo.begin());
    emit undoChanged();
}

void Application::undo(MainWindow& window)
{
    if (m_undo.empty())
        return;
    UndoStep step = std::move(m_undo.back());
    m_undo.pop_back();
    emit undoChanged();
    step.undo(window);
}

MainWindow* Application::openWindow(const QUrl& location, const QString& selectName)
{
    auto owned = std::make_unique<MainWindow>(*this, location, selectName);
    MainWindow* window = owned.get();
    connect(window, &MainWindow::closed, this, [this, window] {
        // Not from inside the window's own close handling; right after it.
        QMetaObject::invokeMethod(
            this,
            [this, window] { std::erase_if(m_windows, [&](const auto& owned) { return owned.get() == window; }); },
            Qt::QueuedConnection);
    });
    m_windows.push_back(std::move(owned));
    window->resize(m_config.window.width, m_config.window.height);
    if (m_config.window.maximized)
        window->showMaximized();
    else
        window->show();
    return window;
}

} // namespace ariadne
