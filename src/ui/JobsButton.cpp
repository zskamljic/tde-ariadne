#include "JobsButton.hpp"

#include "Jobs.hpp"
#include "Theme.hpp"

#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QVBoxLayout>

#include <memory>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

QString describeProgress(const Job& job)
{
    const TransferProgress& progress = job.progress();
    const QLocale locale;
    const auto size = [&](qint64 bytes) { return locale.formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat); };
    QString text = progress.totalBytes > 0 ? u"%1 of %2"_s.arg(size(progress.doneBytes), size(progress.totalBytes))
                                           : u"%1 of %2 files"_s.arg(progress.doneFiles).arg(progress.totalFiles);
    if (const qint64 seconds = job.secondsLeft(); seconds >= 0) {
        text += seconds < 60 ? u" — %1 s left"_s.arg(seconds)
            : seconds < 3600 ? u" — %1 min left"_s.arg((seconds + 59) / 60)
                             : u" — %1 h left"_s.arg(seconds / 3600);
    }
    return text;
}

// One job in the popover.
class JobRow : public QWidget {
public:
    JobRow(Job& job, QWidget* parent)
        : QWidget(parent)
        , m_job(&job)
        , m_title(new QLabel(job.description(), this))
        , m_bar(new QProgressBar(this))
        , m_detail(new QLabel(this))
    {
        m_title->setWordWrap(true);
        m_bar->setRange(0, 1000);
        m_bar->setTextVisible(false);
        m_detail->setObjectName(u"AboutDetails"_s);

        QToolButton* cancel = new QToolButton(this);
        cancel->setObjectName(u"HeaderButton"_s);
        cancel->setIcon(theme::symbolicIcon(u"process-stop"_s));
        cancel->setToolTip(u"Cancel"_s);
        connect(cancel, &QToolButton::clicked, this, [this] {
            if (m_job)
                m_job->cancel();
        });

        auto* text = new QVBoxLayout;
        text->setSpacing(4);
        text->addWidget(m_title);
        text->addWidget(m_bar);
        text->addWidget(m_detail);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addLayout(text, 1);
        layout->addWidget(cancel, 0, Qt::AlignVCenter);

        connect(&job, &Job::progressChanged, this, &JobRow::update);
        update();
    }

private:
    void update()
    {
        const TransferProgress& progress = m_job->progress();
        const double done = progress.totalBytes > 0 ? double(progress.doneBytes) / double(progress.totalBytes)
            : progress.totalFiles > 0               ? double(progress.doneFiles) / double(progress.totalFiles)
                                                    : 0.0;
        m_bar->setValue(int(done * 1000));
        m_detail->setText(describeProgress(*m_job));
    }

    QPointer<Job> m_job;
    QLabel* m_title;
    QProgressBar* m_bar;
    QLabel* m_detail;
};

// A popup with rounded corners. Transparent windows get no stylesheet background, so it
// paints its own.
class Popover : public QFrame {
public:
    explicit Popover(QWidget* parent)
        : QFrame(parent, Qt::Popup)
    {
        setObjectName(u"Popover"_s);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(theme::colors().border, 1));
        painter.setBrush(theme::colors().base);
        const qreal radius = theme::radius(theme::RadiusSize::Large);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
};

} // namespace

JobsButton::JobsButton(JobManager& jobs, QWidget* parent)
    : QToolButton(parent)
    , m_jobs(jobs)
{
    setObjectName(u"HeaderButton"_s);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(34, 34);
    setToolTip(u"File Operations"_s);
    connect(this, &QToolButton::clicked, this, &JobsButton::showPopover);
    connect(&m_jobs, &JobManager::changed, this, &JobsButton::jobsChanged);
    jobsChanged();
}

void JobsButton::jobsChanged()
{
    for (Job* job : m_jobs.jobs())
        connect(job, &Job::progressChanged, this, qOverload<>(&QWidget::update), Qt::UniqueConnection);
    setVisible(!m_jobs.isEmpty());
    if (m_popover) {
        if (m_jobs.isEmpty())
            m_popover->close();
        else
            fillPopover();
    }
    update();
}

double JobsButton::fraction() const
{
    qint64 done = 0;
    qint64 total = 0;
    for (const Job* job : m_jobs.jobs()) {
        done += job->progress().doneBytes;
        total += job->progress().totalBytes;
    }
    return total > 0 ? double(done) / double(total) : 0.0;
}

void JobsButton::paintEvent(QPaintEvent* event)
{
    QToolButton::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF ring = QRectF(0, 0, 16, 16).translated(rect().center() - QPointF(8, 8));
    QColor track = theme::colors().text;
    track.setAlphaF(0.25f);
    painter.setPen(QPen(track, 2.5));
    painter.drawEllipse(ring);
    painter.setPen(QPen(theme::colors().accent, 2.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, 90 * 16, -int(fraction() * 360 * 16));
}

void JobsButton::showPopover()
{
    if (m_popover) {
        m_popover->close();
        return;
    }
    auto* popover = new Popover(this);
    popover->setAttribute(Qt::WA_DeleteOnClose);
    m_popover = popover;
    auto* layout = new QVBoxLayout(popover);
    layout->setContentsMargins(14, 12, 10, 12);
    layout->setSpacing(14);
    popover->setFixedWidth(380);
    fillPopover();
    popover->adjustSize();
    popover->move(mapToGlobal(QPoint(width() - popover->width(), height() + 4)));
    popover->show();
}

void JobsButton::fillPopover()
{
    // Start afresh: jobs come and go.
    QLayout* layout = m_popover->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* row = item->widget())
            row->deleteLater();
        const std::unique_ptr<QLayoutItem> taken(item);
    }
    for (Job* job : m_jobs.jobs())
        layout->addWidget(new JobRow(*job, m_popover));
    m_popover->adjustSize();
}

} // namespace ariadne
