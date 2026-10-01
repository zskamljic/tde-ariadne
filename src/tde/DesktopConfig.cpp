#include "DesktopConfig.hpp"

#include "LuaConfig.hpp"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace tde {
namespace {

DesktopConfig s_desktop;

constexpr std::pair<QStringView, ButtonSide> buttonSides[] = {
    {u"left", ButtonSide::Left},
    {u"right", ButtonSide::Right},
};

constexpr std::pair<QStringView, WindowButton> windowButtons[] = {
    {u"minimize", WindowButton::Minimize},
    {u"maximize", WindowButton::Maximize},
    {u"close", WindowButton::Close},
};

constexpr std::pair<QStringView, QStringView> themes[] = {
    {u"arc-dark", u"arc-dark"},
    {u"arc", u"arc"},
    {u"system", u"system"},
};

void parse(LuaTableReader& reader, DesktopConfig& config)
{
    reader.table("window_buttons", [&] {
        auto& buttons = config.windowButtons;
        if (const auto side = reader.choice<ButtonSide>("position", buttonSides))
            buttons.side = *side;
        reader.table("order", [&] {
            std::vector<WindowButton> order;
            reader.forEachString([&](const QString& name) {
                const auto it = std::ranges::find_if(windowButtons,
                    [&](const auto& entry) { return name.compare(entry.first, Qt::CaseInsensitive) == 0; });
                if (it == std::end(windowButtons))
                    reader.warn(name, u"unknown window button, expected minimize, maximize or close"_s);
                else if (std::ranges::find(order, it->second) == order.end())
                    order.push_back(it->second);
            });
            buttons.order = std::move(order);
        });
    });

    if (const auto terminal = reader.string("terminal"))
        config.terminal = *terminal;

    reader.table("appearance", [&] {
        auto& appearance = config.appearance;
        if (const auto theme = reader.choice<QStringView>("theme", themes))
            appearance.theme = theme->toString();
        if (const auto iconTheme = reader.string("icon_theme"))
            appearance.iconTheme = *iconTheme;
        if (const auto radius = reader.integer("corner_radius", 0, 24))
            appearance.cornerRadius = *radius;
        reader.table("colors", [&] {
            reader.forEachStringPair(
                [&](const QString& name, const QString& value) { appearance.colors.insert(name, value); });
        });
    });
}

} // namespace

QString desktopConfigPath()
{
    return configDirectory() + u"/config.lua"_s;
}

std::expected<QStringList, QString> readDesktopConfig(const QString& path, DesktopConfig& config)
{
    return readLuaConfig(path, [&](LuaTableReader& reader) { parse(reader, config); });
}

DesktopConfig loadDesktopConfig(const QString& path)
{
    DesktopConfig config;
    loadLuaConfig(path, [&](LuaTableReader& reader) { parse(reader, config); });
    return config;
}

const DesktopConfig& desktop()
{
    return s_desktop;
}

void setDesktop(DesktopConfig config)
{
    s_desktop = std::move(config);
}

} // namespace tde
