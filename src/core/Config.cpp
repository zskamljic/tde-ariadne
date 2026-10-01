#include "Config.hpp"

#include "tde/LuaConfig.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr std::pair<QStringView, ViewMode> viewModes[] = {
    {u"grid", ViewMode::Grid},
    {u"list", ViewMode::List},
};

constexpr std::pair<QStringView, SortKey> sortKeys[] = {
    {u"name", SortKey::Name},
    {u"modified", SortKey::Modified},
    {u"size", SortKey::Size},
    {u"type", SortKey::Type},
};

void parse(tde::LuaTableReader& reader, Config& config)
{
    reader.table("window", [&] {
        auto& window = config.window;
        if (const auto width = reader.integer("width", 320, 16384))
            window.width = *width;
        if (const auto height = reader.integer("height", 240, 16384))
            window.height = *height;
        if (const auto maximized = reader.boolean("maximized"))
            window.maximized = *maximized;
        if (const auto width = reader.integer("sidebar_width", 120, 1000))
            window.sidebarWidth = *width;
    });

    reader.table("view", [&] {
        auto& view = config.view;
        if (const auto mode = reader.choice<ViewMode>("mode", viewModes))
            view.mode = *mode;
        if (const auto key = reader.choice<SortKey>("sort", sortKeys))
            view.sortKey = *key;
        if (const auto descending = reader.boolean("descending"))
            view.sortDescending = *descending;
        if (const auto foldersFirst = reader.boolean("folders_first"))
            view.foldersFirst = *foldersFirst;
        if (const auto caseSensitive = reader.boolean("case_sensitive"))
            view.caseSensitive = *caseSensitive;
        if (const auto showHidden = reader.boolean("show_hidden"))
            view.showHidden = *showHidden;
        if (const auto size = reader.integer("grid_icon_size", 32, 256))
            view.gridIconSize = *size;
        if (const auto size = reader.integer("list_icon_size", 16, 64))
            view.listIconSize = *size;
    });

    reader.table("icons", [&] {
        reader.forEachStringPair(
            [&](const QString& mimeType, const QString& icon) { config.icons.insert(mimeType, icon); });
    });
}

QString boolText(bool value)
{
    return value ? u"true"_s : u"false"_s;
}

} // namespace

QString configDirectory()
{
    return tde::configDirectory() + u"/ariadne"_s;
}

QString configPath()
{
    return configDirectory() + u"/config.lua"_s;
}

QString statePath()
{
    return configDirectory() + u"/state.lua"_s;
}

std::expected<QStringList, QString> readConfigFile(const QString& path, Config& config)
{
    return tde::readLuaConfig(path, [&](tde::LuaTableReader& reader) { parse(reader, config); });
}

Config loadConfig(const QString& configFile, const QString& stateFile)
{
    Config config;
    const auto parser = [&](tde::LuaTableReader& reader) { parse(reader, config); };
    tde::loadLuaConfig(configFile, parser);

    const QFileInfo configInfo(configFile);
    const QFileInfo stateInfo(stateFile);
    if (stateInfo.exists() && (!configInfo.exists() || stateInfo.lastModified() >= configInfo.lastModified()))
        tde::loadLuaConfig(stateFile, parser);
    return config;
}

bool saveState(const Config& config, const QString& path)
{
    const auto& window = config.window;
    const auto& view = config.view;
    const auto field = [](const char* key, const QString& value) {
        return u"        %1 = %2,"_s.arg(QLatin1StringView(key), value);
    };

    const QStringList lines {
        u"-- Written by Ariadne to remember the settings last used. Edit config.lua instead:"_s,
        u"-- when config.lua is newer than this file, its settings win."_s,
        u"return {"_s,
        u"    window = {"_s,
        field("width", QString::number(window.width)),
        field("height", QString::number(window.height)),
        field("maximized", boolText(window.maximized)),
        field("sidebar_width", QString::number(window.sidebarWidth)),
        u"    },"_s,
        u"    view = {"_s,
        field("mode", tde::luaString(tde::choiceName<ViewMode>(viewModes, view.mode))),
        field("sort", tde::luaString(tde::choiceName<SortKey>(sortKeys, view.sortKey))),
        field("descending", boolText(view.sortDescending)),
        field("folders_first", boolText(view.foldersFirst)),
        field("case_sensitive", boolText(view.caseSensitive)),
        field("show_hidden", boolText(view.showHidden)),
        field("grid_icon_size", QString::number(view.gridIconSize)),
        field("list_icon_size", QString::number(view.listIconSize)),
        u"    },"_s,
        u"}"_s,
    };
    const QString text = lines.join(u'\n') + u'\n';

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

QString FolderSettings::folderSettingsPath()
{
    return configDirectory() + u"/folders.lua"_s;
}

FolderSettings::FolderSettings(QString path)
    : m_path(std::move(path))
{
    tde::loadLuaConfig(m_path, [&](tde::LuaTableReader& reader) {
        reader.forEachTable([&](const QString& folder) {
            // Folders are remembered by path; forget those that are gone.
            if (folder.startsWith(u'/') && !QFileInfo(folder).isDir())
                return;
            FolderView view;
            if (const auto mode = reader.choice<ViewMode>("mode", viewModes))
                view.mode = *mode;
            if (const auto key = reader.choice<SortKey>("sort", sortKeys))
                view.sortKey = *key;
            if (const auto descending = reader.boolean("descending"))
                view.sortDescending = *descending;
            m_folders.insert(folder, view);
        });
    });
}

std::optional<FolderView> FolderSettings::find(const QString& folder) const
{
    const auto it = m_folders.constFind(folder);
    return it == m_folders.cend() ? std::nullopt : std::optional(*it);
}

void FolderSettings::set(const QString& folder, const FolderView& view)
{
    m_folders.insert(folder, view);
}

void FolderSettings::remove(const QString& folder)
{
    m_folders.remove(folder);
}

bool FolderSettings::save() const
{
    QStringList folders = m_folders.keys();
    folders.sort();
    QStringList lines {
        u"-- Written by Ariadne: how single folders are shown, where they differ from the default."_s,
        u"return {"_s,
    };
    for (const QString& folder : std::as_const(folders)) {
        const FolderView& view = m_folders[folder];
        lines << u"    [%1] = { mode = %2, sort = %3, descending = %4 },"_s.arg(tde::luaString(folder),
            tde::luaString(tde::choiceName<ViewMode>(viewModes, view.mode)),
            tde::luaString(tde::choiceName<SortKey>(sortKeys, view.sortKey)), boolText(view.sortDescending));
    }
    lines << u"}"_s;

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write((lines.join(u'\n') + u'\n').toUtf8());
    return file.commit();
}

} // namespace ariadne
