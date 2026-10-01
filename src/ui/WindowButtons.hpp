#pragma once

#include "tde/DesktopConfig.hpp"

#include <QWidget>

#include <vector>

namespace ariadne {

using tde::WindowButton;

// Minimize / maximize / close buttons in the order given by the config.
class WindowButtons : public QWidget {
    Q_OBJECT

public:
    WindowButtons(const std::vector<WindowButton>& order, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* m_watchedWindow = nullptr;
};

} // namespace ariadne
