#include "Toast.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {

Toast::Toast(QWidget* parent)
    : QFrame(parent)
    , m_label(new QLabel(this))
    , m_button(new QPushButton(this))
{
    setObjectName(u"Toast"_s);
    setAttribute(Qt::WA_StyledBackground);
    m_label->setObjectName(u"ToastText"_s);
    m_label->setWordWrap(true);
    m_button->setObjectName(u"ToastButton"_s);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFocusPolicy(Qt::NoFocus);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 6, 8, 6);
    layout->setSpacing(12);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_button);
    hide();

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
    connect(m_button, &QPushButton::clicked, this, [this] {
        hide();
        if (const auto action = std::exchange(m_action, {}))
            action();
    });
    parent->installEventFilter(this);
}

void Toast::showMessage(const QString& text, int timeoutMs, const QString& actionLabel, std::function<void()> action)
{
    m_label->setText(text);
    m_action = std::move(action);
    m_button->setText(actionLabel);
    m_button->setVisible(!actionLabel.isEmpty() && m_action);
    // Messages with a button stay a little longer, to leave time to use it.
    m_timer.start(m_button->isVisible() ? std::max(timeoutMs, 7000) : timeoutMs);
    reposition();
    show();
    raise();
}

bool Toast::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent() && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QFrame::eventFilter(watched, event);
}

void Toast::reposition()
{
    const QWidget* area = parentWidget();
    const int maximum = std::max(160, area->width() - 48);
    setMaximumWidth(maximum);
    // Wrap only what does not fit on one line; wrapping labels otherwise pick a narrow width.
    const int buttonWidth = m_button->isVisible() ? m_button->sizeHint().width() + 12 : 0;
    const int textRoom = maximum - 24 - buttonWidth;
    m_label->setWordWrap(m_label->fontMetrics().horizontalAdvance(m_label->text()) > textRoom);
    adjustSize();
    move((area->width() - width()) / 2, area->height() - height() - 24);
}

} // namespace ariadne
