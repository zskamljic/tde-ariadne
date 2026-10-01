#pragma once

#include <QPointer>
#include <QToolButton>

class QFrame;

namespace ariadne {

class JobManager;

// Shown in the header bar while files are being copied or moved: a ring filling up with the
// overall progress, and a popover with each job's details and a cancel button.
class JobsButton : public QToolButton {
    Q_OBJECT

public:
    JobsButton(JobManager& jobs, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void jobsChanged();
    void showPopover();
    void fillPopover();
    double fraction() const;

    JobManager& m_jobs;
    QPointer<QFrame> m_popover;
};

} // namespace ariadne
