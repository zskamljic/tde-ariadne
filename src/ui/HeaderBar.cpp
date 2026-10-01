#include "HeaderBar.hpp"

#include "Theme.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>
#include <QWindow>

using namespace Qt::StringLiterals;

namespace ariadne {

HeaderBar::HeaderBar(QWidget* parent)
    : QWidget(parent)
    , m_layout(new QHBoxLayout(this))
{
    setObjectName(u"HeaderBar"_s);
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(47);
    m_layout->setContentsMargins(6, 6, 6, 7);
    m_layout->setSpacing(6);
}

QToolButton* HeaderBar::makeButton(const QString& iconName, const QString& toolTip, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(u"HeaderButton"_s);
    button->setIcon(theme::symbolicIcon(iconName));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(34);
    button->setMinimumWidth(34);
    return button;
}

void HeaderBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_pressPosition = event->position();
    event->accept();
}

void HeaderBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressPosition || !(event->buttons() & Qt::LeftButton))
        return;
    if ((event->position() - *m_pressPosition).manhattanLength() < QApplication::startDragDistance())
        return;
    m_pressPosition.reset();
    if (QWindow* handle = window()->windowHandle())
        handle->startSystemMove();
}

void HeaderBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_pressPosition.reset();
    event->accept();
}

void HeaderBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget* top = window();
    if (event->button() != Qt::LeftButton || top->minimumSize() == top->maximumSize())
        return;
    top->isMaximized() ? top->showNormal() : top->showMaximized();
}

} // namespace ariadne
