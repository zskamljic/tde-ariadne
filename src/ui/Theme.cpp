#include "Theme.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QIconEngine>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <print>

using namespace Qt::StringLiterals;

namespace ariadne::theme {
namespace {

Colors s_colors;
int s_cornerRadius = 5;

Colors arcDark()
{
    Colors c;
    c.window = QColor(0x383c4a);
    c.base = QColor(0x404552);
    c.header = QColor(0x2f343f);
    c.sidebar = QColor(0x353945);
    c.sidebarText = QColor(0xbac3cf);
    c.text = QColor(0xd3dae3);
    c.dimText = QColor(0x8a939f);
    c.accent = QColor(0x5294e2);
    c.accentText = QColor(0xffffff);
    c.border = QColor(0x2b2e39);
    c.hover = QColor(255, 255, 255, 18);
    c.pressed = QColor(255, 255, 255, 34);
    c.entry = QColor(0x383c4a);
    c.scrollbar = QColor(0x767b87);
    c.closeHover = QColor(0xcc575d);
    c.error = QColor(0xfc4138);
    return c;
}

Colors arcLight()
{
    Colors c;
    c.window = QColor(0xf5f6f7);
    c.base = QColor(0xffffff);
    c.header = QColor(0xe7e8eb);
    c.sidebar = QColor(0xf0f1f3);
    c.sidebarText = QColor(0x5c616c);
    c.text = QColor(0x5c616c);
    c.dimText = QColor(0xa0a4ab);
    c.accent = QColor(0x5294e2);
    c.accentText = QColor(0xffffff);
    c.border = QColor(0xd6d9de);
    c.hover = QColor(0, 0, 0, 13);
    c.pressed = QColor(0, 0, 0, 26);
    c.entry = QColor(0xffffff);
    c.scrollbar = QColor(0xb8babf);
    c.closeHover = QColor(0xf46067);
    c.error = QColor(0xfc4138);
    return c;
}

struct NamedColor {
    QStringView name;
    QColor Colors::* member;
};

const NamedColor namedColors[] = {
    {u"window", &Colors::window},
    {u"base", &Colors::base},
    {u"header", &Colors::header},
    {u"sidebar", &Colors::sidebar},
    {u"sidebar_text", &Colors::sidebarText},
    {u"text", &Colors::text},
    {u"dim_text", &Colors::dimText},
    {u"accent", &Colors::accent},
    {u"accent_text", &Colors::accentText},
    {u"border", &Colors::border},
    {u"hover", &Colors::hover},
    {u"pressed", &Colors::pressed},
    {u"entry", &Colors::entry},
    {u"scrollbar", &Colors::scrollbar},
    {u"close_hover", &Colors::closeHover},
    {u"error", &Colors::error},
};

// `@name@` placeholders are replaced with the colours above.
const QString StyleSheetTemplate = uR"(
* { outline: 0; }
QWidget#MainWindow { background: @border@; }
QWidget#SidebarColumn { background: @sidebar@; }
QWidget#ContentColumn { background: @base@; }
QWidget#HeaderBar { background: @header@; border-bottom: 1px solid @border@; }

QListWidget#Sidebar { background: @sidebar@; color: @sidebar_text@; border: none; }

QToolButton#HeaderButton {
    background: transparent; color: @text@;
    border: 1px solid transparent; border-radius: @radius@; padding: 5px;
}
QToolButton#HeaderButton:hover { background: @hover@; border-color: @border@; }
QToolButton#HeaderButton:pressed, QToolButton#HeaderButton:checked, QToolButton#HeaderButton:open {
    background: @pressed@; border-color: @border@;
}
QToolButton#HeaderButton:disabled { border-color: transparent; background: transparent; }
QToolButton#HeaderButton[popupMode="1"] { padding-right: 18px; }
QToolButton#HeaderButton::menu-button { border: none; width: 16px; }
QToolButton#HeaderButton::menu-indicator { image: none; width: 0; }

QFrame#PathBar { background: @entry@; border: 1px solid @border@; border-radius: @radius@; }
QFrame#PathBar[editing="true"] { border-color: @accent@; }
QFrame#PathBar[error="true"] { border-color: @error@; }
QScrollArea#CrumbArea, QWidget#CrumbContainer { background: transparent; border: none; }
QToolButton#Crumb {
    background: transparent; color: @dim_text@;
    border: none; border-radius: @radius_small@; padding: 3px 7px;
}
QToolButton#Crumb:hover { background: @hover@; color: @text@; }
QToolButton#Crumb[current="true"] { color: @text@; font-weight: bold; }
QLabel#CrumbSeparator { color: @dim_text@; }
QLineEdit#PathEntry {
    background: transparent; color: @text@; border: none; padding: 0 8px;
    selection-background-color: @accent@; selection-color: @accent_text@;
}

QListView#GridView, QTreeView#ListView { background: @base@; color: @text@; border: none; }
QTreeView#ListView::item { border: none; padding: 3px 0; }
QTreeView#ListView::item:hover { background: @hover@; }
QTreeView#ListView::item:selected { background: @accent@; color: @accent_text@; }
QHeaderView { background: @base@; border: none; }
QHeaderView::section {
    background: @base@; color: @dim_text@;
    border: none; border-bottom: 1px solid @border@; padding: 6px 8px;
}
QHeaderView::section:hover { color: @text@; }
QHeaderView::down-arrow, QHeaderView::up-arrow { width: 10px; height: 10px; }

QSplitter::handle { background: @border@; }

QMenu {
    background: @base@; color: @text@;
    border: 1px solid @border@; border-radius: @radius_large@; padding: 5px;
}
QMenu::item { padding: 6px 24px 6px 8px; border-radius: @radius_small@; background: transparent; }
QMenu::icon, QMenu::indicator { padding-left: 10px; }
QMenu::item:selected { background: @accent@; color: @accent_text@; }
QMenu::item:disabled { color: @dim_text@; }
QMenu::separator { height: 1px; background: @border@; margin: 5px 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: @scrollbar@; border-radius: @radius_scrollbar@; margin: 2px; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:hover { background: @accent@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QToolTip {
    background: @header@; color: @text@;
    border: 1px solid @border@; border-radius: @radius@; padding: 4px 6px;
}

QDialog#Dialog { background: @border@; }
QWidget#DialogBody { background: @window@; }
QLabel#DialogTitle { font-weight: bold; }
QLabel#AboutName { font-size: 18pt; font-weight: bold; }
QLabel#AboutDetails { color: @dim_text@; }
QLineEdit {
    background: @entry@; color: @text@; border: 1px solid @border@; border-radius: @radius_small@; padding: 5px;
    selection-background-color: @accent@; selection-color: @accent_text@;
}
QLineEdit:focus { border-color: @accent@; }
QPushButton {
    background: @header@; color: @text@;
    border: 1px solid @border@; border-radius: @radius_small@; padding: 6px 14px;
}
QPushButton:hover { background: @base@; }
QPushButton:default { background: @accent@; color: @accent_text@; border-color: @accent@; }
QPushButton#DestructiveButton { background: @error@; color: @accent_text@; border-color: @error@; }
QPushButton#DestructiveButton:hover { background: @close_hover@; }
QLabel#ConfirmMessage { font-weight: bold; }

QLabel#PlaceholderText { color: @dim_text@; font-size: 15pt; font-weight: bold; }
QWidget#InfoBar { background: @window@; border-bottom: 1px solid @border@; }
QLineEdit#InlineEditor {
    background: @entry@; border: 1px solid @accent@; border-radius: @radius_small@; padding: 1px 4px;
}
QLineEdit#SearchEntry { border-radius: @radius@; padding: 6px 8px; }
QLabel#StatusBar {
    background: @header@; color: @text@;
    border: 1px solid @border@; border-radius: @radius@; padding: 5px 10px;
}
QFrame#Toast { background: @header@; border: 1px solid @border@; border-radius: @radius_large@; }
QLabel#ToastText { color: @text@; padding: 3px 0; }
QPushButton#ToastButton {
    background: transparent; color: @accent@; border: none; font-weight: bold; padding: 4px 8px;
}
QPushButton#ToastButton:hover { background: @hover@; border-radius: @radius_small@; }
QProgressBar {
    background: @window@; border: none; border-radius: 3px; height: 6px; max-height: 6px;
}
QProgressBar::chunk { background: @accent@; border-radius: 3px; }
)"_s;

QString cssColor(const QColor& color)
{
    if (color.alpha() == 255)
        return color.name();
    return u"rgba(%1, %2, %3, %4)"_s.arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QString styleSheet(const Colors& colors)
{
    QString sheet = StyleSheetTemplate;
    for (const auto& [name, member] : namedColors)
        sheet.replace(u'@' + name.toString() + u'@', cssColor(colors.*member));
    const auto px = [](int value) { return u"%1px"_s.arg(value); };
    sheet.replace(u"@radius@"_s, px(radius(RadiusSize::Normal)));
    sheet.replace(u"@radius_small@"_s, px(radius(RadiusSize::Small)));
    sheet.replace(u"@radius_large@"_s, px(radius(RadiusSize::Large)));
    sheet.replace(u"@radius_scrollbar@"_s, px(std::min(radius(RadiusSize::Normal), 3)));
    return sheet;
}

QPalette palette(const Colors& c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.window);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.header);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, c.accentText);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.accentText);
    p.setColor(QPalette::ToolTipBase, c.header);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.dimText);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Light, c.base.lighter(115));
    p.setColor(QPalette::Midlight, c.base);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border.darker(110));
    p.setColor(QPalette::Shadow, Qt::black);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.dimText);
    return p;
}

class AppStyle : public QProxyStyle {
public:
    AppStyle()
        : QProxyStyle(QStyleFactory::create(u"Fusion"_s))
    {
    }

    int styleHint(
        StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override
    {
        switch (hint) {
        case SH_ItemView_ActivateItemOnSingleClick:
        case SH_DialogButtonBox_ButtonsHaveIcons:
            return 0;
        case SH_DialogButtonLayout:
            return QDialogButtonBox::GnomeLayout; // Cancel on the left, the action on the right
        default:
            return QProxyStyle::styleHint(hint, option, widget, returnData);
        }
    }

    void polish(QWidget* widget) override
    {
        // Menus and tooltips are windows of their own; they need an alpha channel for
        // rounded corners.
        const bool popup
            = qobject_cast<QMenu*>(widget) || widget->windowType() == Qt::ToolTip || widget->objectName() == u"Popover";
        if (popup && !widget->testAttribute(Qt::WA_WState_Created)) {
            widget->setAttribute(Qt::WA_TranslucentBackground);
            widget->setWindowFlag(Qt::NoDropShadowWindowHint);
        }
        QProxyStyle::polish(widget);
    }
};

// Recolours a (symbolic) icon at paint time, so it follows the current colours.
class SymbolicIconEngine : public QIconEngine {
public:
    explicit SymbolicIconEngine(QIcon source)
        : m_source(std::move(source))
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap = m_source.pixmap(size, scale, QIcon::Normal, state);
        if (pixmap.isNull())
            return pixmap;

        QColor color = mode == QIcon::Selected ? colors().accentText : colors().text;
        if (mode == QIcon::Disabled)
            color.setAlphaF(0.4f);

        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRect(QPoint(), pixmap.size()), color);
        return pixmap;
    }

    QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return m_source.actualSize(size, mode, state);
    }

    QIconEngine* clone() const override { return std::make_unique<SymbolicIconEngine>(m_source).release(); }
    QString key() const override { return u"ariadne-symbolic"_s; }

private:
    QIcon m_source;
};

QString gtkIconTheme()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const auto* gtk : {"gtk-4.0", "gtk-3.0"}) {
        const QSettings settings(u"%1/%2/settings.ini"_s.arg(configDir, QLatin1StringView(gtk)), QSettings::IniFormat);
        const QString name = settings.value(u"Settings/gtk-icon-theme-name"_s).toString();
        if (!name.isEmpty())
            return name;
    }
    return {};
}

void setupIconTheme(const QString& configured)
{
    // Without a desktop platform theme Qt only searches its resources.
    QStringList searchPaths = QIcon::themeSearchPaths();
    QStringList candidates {QDir::homePath() + u"/.icons"_s};
    for (const QString& dataDir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        candidates << dataDir + u"/icons"_s;
    for (const QString& path : std::as_const(candidates)) {
        if (!searchPaths.contains(path) && QFileInfo(path).isDir())
            searchPaths << path;
    }
    QIcon::setThemeSearchPaths(searchPaths);

    QString name = configured;
    if (name.isEmpty()) {
        const QString platform = QIcon::themeName();
        name = platform.isEmpty() || platform == u"hicolor" ? gtkIconTheme() : platform;
    }
    if (name.isEmpty())
        name = u"Adwaita"_s;
    QIcon::setThemeName(name);
    QIcon::setFallbackThemeName(u"Adwaita"_s);
}

} // namespace

const Colors& colors()
{
    return s_colors;
}

int radius(RadiusSize size)
{
    switch (size) {
    case RadiusSize::Small:
        return std::max(0, s_cornerRadius - 1);
    case RadiusSize::Normal:
        return s_cornerRadius;
    case RadiusSize::Large:
        return s_cornerRadius == 0 ? 0 : s_cornerRadius + 3;
    }
    return s_cornerRadius;
}

void apply(QApplication& app, const tde::DesktopConfig::Appearance& appearance)
{
    s_cornerRadius = appearance.cornerRadius;
    QString name = appearance.theme;
    if (name == u"system")
        name = app.styleHints()->colorScheme() == Qt::ColorScheme::Light ? u"arc"_s : u"arc-dark"_s;
    s_colors = name == u"arc" ? arcLight() : arcDark();

    for (auto it = appearance.colors.cbegin(); it != appearance.colors.cend(); ++it) {
        const auto named = std::ranges::find(namedColors, it.key(), &NamedColor::name);
        const QColor color = QColor::fromString(it.value());
        if (named == std::end(namedColors))
            std::println(stderr, "ariadne: unknown colour name \"{}\"", it.key().toStdString());
        else if (!color.isValid())
            std::println(
                stderr, "ariadne: invalid colour \"{}\" for {}", it.value().toStdString(), it.key().toStdString());
        else
            s_colors.*(named->member) = color;
    }

    setupIconTheme(appearance.iconTheme);
    QApplication::setStyle(std::make_unique<AppStyle>().release()); // the application owns it
    QApplication::setPalette(palette(s_colors));
    app.setStyleSheet(styleSheet(s_colors));
}

QIcon symbolicIcon(const QString& name)
{
    const QString symbolic = name.endsWith(u"-symbolic") ? name : name + u"-symbolic"_s;
    for (const QString& candidate : {symbolic, symbolic.chopped(9)}) {
        if (QIcon::hasThemeIcon(candidate))
            return QIcon(std::make_unique<SymbolicIconEngine>(QIcon::fromTheme(candidate)).release()); // QIcon owns it
    }
    return {};
}

QIcon themeIcon(std::initializer_list<QString> names)
{
    for (const QString& name : names) {
        if (QIcon::hasThemeIcon(name))
            return QIcon::fromTheme(name);
    }
    return {};
}

} // namespace ariadne::theme
