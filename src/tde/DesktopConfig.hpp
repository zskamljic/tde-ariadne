#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <expected>

#include <vector>

namespace tde {

enum class ButtonSide { Left, Right };
enum class WindowButton { Minimize, Maximize, Close };

// Settings shared by all TDE applications, from ~/.config/tde/config.lua.
struct DesktopConfig {
    struct WindowButtons {
        ButtonSide side = ButtonSide::Right;
        std::vector<WindowButton> order {WindowButton::Minimize, WindowButton::Maximize, WindowButton::Close};
    };

    struct Appearance {
        QString theme = QStringLiteral("arc-dark");
        QString iconTheme;
        QHash<QString, QString> colors;
        int cornerRadius = 5;
    };

    WindowButtons windowButtons;
    Appearance appearance;
    QString terminal; // program to open terminals with; empty picks one
};

QString desktopConfigPath();

// Reads the file at `path` over the settings in `config`; returns the warnings, or an error.
std::expected<QStringList, QString> readDesktopConfig(const QString& path, DesktopConfig& config);

// Loads the desktop config, reporting problems on stderr and using defaults for the rest.
DesktopConfig loadDesktopConfig(const QString& path = desktopConfigPath());

// The desktop config of this process, set once at startup.
const DesktopConfig& desktop();
void setDesktop(DesktopConfig config);

} // namespace tde
