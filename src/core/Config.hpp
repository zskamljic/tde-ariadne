#pragma once

#include "CustomActions.hpp"

#include <QHash>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>
#include <vector>

namespace ariadne {

enum class ViewMode { Grid, List };
enum class SortKey { Name, Modified, Size, Type };

// Ariadne's own settings, from ~/.config/tde/ariadne. Desktop-wide settings such as the
// theme and window buttons live in tde::DesktopConfig.
struct Config {
    struct Window {
        int width = 1100;
        int height = 700;
        bool maximized = false;
        int sidebarWidth = 220;

        bool operator==(const Window&) const = default;
    };

    struct View {
        ViewMode mode = ViewMode::Grid;
        SortKey sortKey = SortKey::Name;
        bool sortDescending = false;
        bool foldersFirst = true;
        bool caseSensitive = false;
        bool showHidden = false;
        int gridIconSize = 64;
        int listIconSize = 24;
        bool expandableFolders = true; // in the list view; from the config file only
        bool archivesAsFolders = true; // opening an archive shows what is in it; config file only

        bool operator==(const View&) const = default;
    };

    Window window;
    View view;
    // MIME type → icon name, replacing the icon theme's choice. Not remembered in the state.
    QHash<QString, QString> icons;
    // The user's own commands in the context menu. Not remembered in the state.
    std::vector<CustomAction> actions;

    bool operator==(const Config&) const = default;
};

QString configDirectory();
// Written by hand.
QString configPath();
// Written by Ariadne, to remember changes made in the user interface.
QString statePath();

// Reads the file at `path` over the settings in `config`; returns the warnings, or an error.
std::expected<QStringList, QString> readConfigFile(const QString& path, Config& config);

// Defaults, overridden by the config file, overridden by the remembered state unless the
// config file was edited after the state was saved. Problems are reported on stderr.
Config loadConfig(const QString& configFile = configPath(), const QString& stateFile = statePath());

bool saveState(const Config& config, const QString& path = statePath());

// How one folder is shown, when it differs from the default.
struct FolderView {
    ViewMode mode = ViewMode::Grid;
    SortKey sortKey = SortKey::Name;
    bool sortDescending = false;

    bool operator==(const FolderView&) const = default;
};

// View settings remembered for single folders, in folders.lua next to the config. Written
// by Ariadne; folders that no longer exist are dropped when it is read.
class FolderSettings {
public:
    explicit FolderSettings(QString path = folderSettingsPath());

    static QString folderSettingsPath();

    std::optional<FolderView> find(const QString& folder) const;
    void set(const QString& folder, const FolderView& view);
    void remove(const QString& folder);
    bool save() const;

private:
    QString m_path;
    QHash<QString, FolderView> m_folders;
};

} // namespace ariadne
