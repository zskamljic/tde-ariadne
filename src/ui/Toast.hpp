#pragma once

#include <QFrame>
#include <QTimer>

#include <functional>

class QLabel;
class QPushButton;

namespace ariadne {

// A short message floating at the bottom of its parent widget, optionally with a button
// such as "Undo".
class Toast : public QFrame {
    Q_OBJECT

public:
    explicit Toast(QWidget* parent);

    void showMessage(
        const QString& text, int timeoutMs = 4000, const QString& actionLabel = {}, std::function<void()> action = {});

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reposition();

    QLabel* m_label;
    QPushButton* m_button;
    std::function<void()> m_action;
    QTimer m_timer;
};

} // namespace ariadne
