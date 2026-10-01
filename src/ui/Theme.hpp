#pragma once

#include "tde/DesktopConfig.hpp"

#include <QColor>
#include <QIcon>
#include <QString>

#include <initializer_list>

class QApplication;

namespace ariadne::theme {

struct Colors {
    QColor window;
    QColor base;
    QColor header;
    QColor sidebar;
    QColor sidebarText;
    QColor text;
    QColor dimText;
    QColor accent;
    QColor accentText;
    QColor border;
    QColor hover;
    QColor pressed;
    QColor entry;
    QColor scrollbar;
    QColor closeHover;
    QColor error;
};

const Colors& colors();

// Corner radii derived from the desktop-wide corner_radius setting.
enum class RadiusSize {
    Small, // menu items, text entries, path crumbs
    Normal, // buttons, path bar, sidebar rows, selections
    Large, // grid icon highlights, notifications
};
int radius(RadiusSize size = RadiusSize::Normal);

// Sets up style, palette, stylesheet and icon theme for the whole application.
void apply(QApplication& app, const tde::DesktopConfig::Appearance& appearance);

// A monochrome icon from the icon theme, recoloured to match the text colour (and white
// when drawn selected). Falls back to the non-symbolic icon; null if neither exists.
QIcon symbolicIcon(const QString& name);

// The first of `names` the icon theme has, in full colour.
QIcon themeIcon(std::initializer_list<QString> names);

} // namespace ariadne::theme
