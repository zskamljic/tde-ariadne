#include "FramelessHelper.hpp"

#include "Theme.hpp"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace ariadne {
namespace {

constexpr int ResizeBorder = 5;

Qt::CursorShape cursorFor(Qt::Edges edges)
{
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
        return Qt::SizeFDiagCursor;
    if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
        return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

// Sits on top of one corner of the window, clears the pixels outside the rounded corner to
// transparent and draws the window border along the curve.
class WindowCorner : public QWidget {
public:
    WindowCorner(Qt::Corner corner, QWidget* window)
        : QWidget(window)
        , m_corner(corner)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    Qt::Corner corner() const { return m_corner; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const qreal r = width();
        QPointF center;
        switch (m_corner) {
        case Qt::TopLeftCorner:
            center = {r, r};
            break;
        case Qt::TopRightCorner:
            center = {0, r};
            break;
        case Qt::BottomLeftCorner:
            center = {r, 0};
            break;
        case Qt::BottomRightCorner:
            center = {0, 0};
            break;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath outside;
        outside.addRect(rect());
        QPainterPath inside;
        inside.addEllipse(center, r, r);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillPath(outside.subtracted(inside), Qt::transparent);

        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setPen(QPen(theme::colors().border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, r - 0.5, r - 0.5);
    }

private:
    Qt::Corner m_corner;
};

} // namespace

FramelessHelper::FramelessHelper(QWidget* window)
    : QObject(window)
    , m_window(window)
{
    m_window->setWindowFlag(Qt::FramelessWindowHint);
    // Must be set before the native window exists, for it to get an alpha channel.
    m_window->setAttribute(Qt::WA_TranslucentBackground);
    // The native window is made when the widget is first shown. Forcing it earlier with
    // winId() would mark the widget native, and Qt then makes its sibling widgets native
    // too: for a dialog, that is its parent window's contents, which would become opaque
    // surfaces over the rounded corners.
    m_window->installEventFilter(this);
    watchWindowHandle();

    for (const auto corner : {Qt::TopLeftCorner, Qt::TopRightCorner, Qt::BottomLeftCorner, Qt::BottomRightCorner})
        m_corners << new WindowCorner(corner, m_window);
    updateCorners();
}

FramelessHelper::~FramelessHelper()
{
    setEdgeCursor({});
}

bool FramelessHelper::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::Show:
            watchWindowHandle();
            updateCorners();
            break;
        case QEvent::Resize:
        case QEvent::WindowStateChange:
            updateCorners();
            break;
        case QEvent::Paint: {
            // A translucent window gets no stylesheet background, so paint the frame here;
            // the window's own layout margin leaves it visible as a one pixel border.
            QPainter painter(m_window);
            painter.fillRect(m_window->rect(), theme::colors().border);
            break;
        }
        case QEvent::ChildAdded:
            // Widgets added later would cover the corners; lift them back up afterwards.
            QTimer::singleShot(0, this, &FramelessHelper::updateCorners);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->buttons() == Qt::NoButton)
            setEdgeCursor(edgesAt(mouse->position()));
        break;
    }
    case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const Qt::Edges edges = edgesAt(mouse->position());
        if (mouse->button() == Qt::LeftButton && edges) {
            setEdgeCursor({});
            m_window->windowHandle()->startSystemResize(edges);
            return true;
        }
        break;
    }
    case QEvent::Leave:
        setEdgeCursor({});
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

Qt::Edges FramelessHelper::edgesAt(const QPointF& position) const
{
    if (m_window->isMaximized() || m_window->isFullScreen() || m_window->minimumSize() == m_window->maximumSize())
        return {};

    Qt::Edges edges;
    const QSize size = m_window->size();
    if (position.x() < ResizeBorder)
        edges |= Qt::LeftEdge;
    if (position.x() >= size.width() - ResizeBorder)
        edges |= Qt::RightEdge;
    if (position.y() < ResizeBorder)
        edges |= Qt::TopEdge;
    if (position.y() >= size.height() - ResizeBorder)
        edges |= Qt::BottomEdge;
    return edges;
}

void FramelessHelper::watchWindowHandle()
{
    QWindow* handle = m_window->windowHandle();
    if (handle && handle != m_watchedHandle) {
        handle->installEventFilter(this);
        m_watchedHandle = handle;
    }
}

void FramelessHelper::updateCorners()
{
    const bool square = m_window->isMaximized() || m_window->isFullScreen();
    const int radius = theme::radius(theme::RadiusSize::Large);
    const QSize size = m_window->size();
    for (QWidget* widget : std::as_const(m_corners)) {
        auto* corner = static_cast<WindowCorner*>(widget);
        if (square || radius == 0) {
            corner->hide();
            continue;
        }
        const int x = (corner->corner() == Qt::TopLeftCorner || corner->corner() == Qt::BottomLeftCorner)
            ? 0
            : size.width() - radius;
        const int y = (corner->corner() == Qt::TopLeftCorner || corner->corner() == Qt::TopRightCorner)
            ? 0
            : size.height() - radius;
        corner->setGeometry(x, y, radius, radius);
        corner->show();
        corner->raise();
    }
}

void FramelessHelper::setEdgeCursor(Qt::Edges edges)
{
    if (edges == m_cursorEdges)
        return;
    if (!m_cursorEdges)
        QGuiApplication::setOverrideCursor(cursorFor(edges));
    else if (edges)
        QGuiApplication::changeOverrideCursor(cursorFor(edges));
    else
        QGuiApplication::restoreOverrideCursor();
    m_cursorEdges = edges;
}

} // namespace ariadne
