#include "WindowButtons.hpp"

#include "Theme.hpp"

#include <QAbstractButton>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

class TitleButton : public QAbstractButton {
public:
    TitleButton(WindowButton kind, QWidget* parent)
        : QAbstractButton(parent)
        , m_kind(kind)
    {
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(sizeHint());
        switch (kind) {
        case WindowButton::Minimize:
            setToolTip(u"Minimize"_s);
            break;
        case WindowButton::Maximize:
            setToolTip(u"Maximize"_s);
            break;
        case WindowButton::Close:
            setToolTip(u"Close"_s);
            break;
        }
    }

    QSize sizeHint() const override { return {26, 26}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = theme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const bool hovered = underMouse();
        const bool pressed = isDown();
        QColor background;
        QColor foreground = colors.text;
        if (m_kind == WindowButton::Close && (hovered || pressed)) {
            background = pressed ? colors.closeHover.darker(115) : colors.closeHover;
            foreground = Qt::white;
        } else if (pressed) {
            background = colors.pressed;
        } else if (hovered) {
            background = colors.hover;
        }

        const QRectF circle = QRectF(rect()).adjusted(2, 2, -2, -2);
        if (background.isValid()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(background);
            // Round at the default radius, square at zero.
            const qreal radius = std::min<qreal>(2.0 * theme::radius(), circle.width() / 2);
            painter.drawRoundedRect(circle, radius, radius);
        }

        painter.setPen(QPen(foreground, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF c = circle.center();
        constexpr qreal s = 4.0;
        switch (m_kind) {
        case WindowButton::Close:
            painter.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
            painter.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
            break;
        case WindowButton::Minimize:
            painter.drawLine(c + QPointF(-s, s - 1), c + QPointF(s, s - 1));
            break;
        case WindowButton::Maximize:
            if (window()->isMaximized()) {
                painter.drawRect(QRectF(c.x() - s, c.y() - s + 2, 2 * s - 2, 2 * s - 2));
                painter.drawPolyline(
                    QList<QPointF> {c + QPointF(-s + 2, -s), c + QPointF(s, -s), c + QPointF(s, s - 2)});
            } else {
                painter.drawRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s));
            }
            break;
        }
    }

private:
    WindowButton m_kind;
};

} // namespace

WindowButtons::WindowButtons(const std::vector<WindowButton>& order, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    for (const WindowButton kind : order) {
        auto* button = new TitleButton(kind, this);
        layout->addWidget(button);
        connect(button, &QAbstractButton::clicked, this, [this, kind] {
            QWidget* top = window();
            switch (kind) {
            case WindowButton::Minimize:
                top->showMinimized();
                break;
            case WindowButton::Maximize:
                top->isMaximized() ? top->showNormal() : top->showMaximized();
                break;
            case WindowButton::Close:
                top->close();
                break;
            }
        });
    }
    // Only ever hide: showing a widget that has no parent yet would make it a window of
    // its own, and a native one once it is moved into a real window.
    if (order.empty())
        hide();
}

void WindowButtons::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_watchedWindow != window()) {
        if (m_watchedWindow)
            m_watchedWindow->removeEventFilter(this);
        m_watchedWindow = window();
        m_watchedWindow->installEventFilter(this);
    }
}

bool WindowButtons::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_watchedWindow && event->type() == QEvent::WindowStateChange) {
        for (QWidget* child : findChildren<QWidget*>())
            child->update();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace ariadne
