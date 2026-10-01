#pragma once

#include <QPointF>
#include <QWidget>

#include <optional>

class QHBoxLayout;
class QToolButton;

namespace ariadne {

// A title bar replacement: holds buttons, moves the window when dragged and toggles
// maximization on double click.
class HeaderBar : public QWidget {
    Q_OBJECT

public:
    explicit HeaderBar(QWidget* parent = nullptr);

    QHBoxLayout* contentLayout() const { return m_layout; }

    static QToolButton* makeButton(const QString& iconName, const QString& toolTip, QWidget* parent);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QHBoxLayout* m_layout;
    std::optional<QPointF> m_pressPosition;
};

} // namespace ariadne
