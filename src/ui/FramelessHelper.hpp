#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QWindow>

class QWidget;

namespace ariadne {

// Removes the system frame from a top-level widget, rounds its corners (except when
// maximized) and lets the user resize it by dragging its edges.
class FramelessHelper : public QObject {
    Q_OBJECT

public:
    explicit FramelessHelper(QWidget* window);
    ~FramelessHelper() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Qt::Edges edgesAt(const QPointF& position) const;
    void setEdgeCursor(Qt::Edges edges);
    void updateCorners();
    void watchWindowHandle();

    QWidget* m_window;
    Qt::Edges m_cursorEdges;
    QList<QWidget*> m_corners;
    QPointer<QWindow> m_watchedHandle;
};

} // namespace ariadne
