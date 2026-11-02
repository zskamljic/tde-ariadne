#include "Jobs.hpp"

#include <chrono>
#include <future>

using namespace std::chrono_literals;

namespace ariadne {

Job::Job(TransferKind kind, QList<TransferItem> items, QString description, Asker ask)
    : m_transfer(std::make_unique<Transfer>(kind, std::move(items)))
    , m_description(std::move(description))
    , m_ask(std::move(ask))
{
}

Job::Job(QString description, Work work)
    : m_work(std::move(work))
    , m_description(std::move(description))
{
}

Job::~Job()
{
    cancel();
}

void Job::cancel()
{
    m_cancelled = true;
    if (m_transfer)
        m_transfer->cancel();
}

void Job::report(const TransferProgress& progress)
{
    constexpr qint64 Interval = 50; // ms
    const bool last = progress.totalFiles > 0 && progress.doneFiles >= progress.totalFiles;
    if (m_lastReport.isValid() && m_lastReport.elapsed() < Interval && !last)
        return;
    m_lastReport.start();
    QMetaObject::invokeMethod(
        this,
        [this, progress] {
            m_progress = progress;
            emit progressChanged();
        },
        Qt::QueuedConnection);
}

void Job::start()
{
    m_elapsed.start();
    const auto done = [this](TransferResult result) {
        QMetaObject::invokeMethod(
            this,
            [this, result = std::move(result)] {
                m_result = result;
                m_finished = true;
                emit finished();
            },
            Qt::QueuedConnection);
    };

    if (!m_transfer) {
        m_worker = std::jthread([this, done] {
            m_work(JobControl(
                [this](const TransferProgress& progress) { report(progress); }, [this] { return m_cancelled.load(); }));
            done({.cancelled = m_cancelled});
        });
        return;
    }

    // Called on the worker thread; hands the numbers over to this object's thread.
    m_transfer->setProgressCallback([this](const TransferProgress& progress) { report(progress); });

    // The worker asks on this object's thread and waits for the answer, giving up when the
    // job is cancelled meanwhile, so it can never block anyone waiting for it to finish.
    m_transfer->setConflictCallback([this, ask = m_ask](const Conflict& conflict) {
        auto promise = std::make_shared<std::promise<ConflictAnswer>>();
        std::future<ConflictAnswer> answer = promise->get_future();
        QMetaObject::invokeMethod(
            this, [promise, ask, conflict] { promise->set_value(ask ? ask(conflict) : ConflictAnswer {}); },
            Qt::QueuedConnection);
        while (answer.wait_for(50ms) != std::future_status::ready) {
            if (m_transfer->isCancelled())
                return ConflictAnswer {ConflictChoice::Cancel, false};
        }
        return answer.get();
    });

    m_worker = std::jthread([this, done] { done(m_transfer->run()); });
}

qint64 Job::secondsLeft() const
{
    // By the bytes, or by the files for work that does not count bytes.
    const bool bytes = m_progress.totalBytes > 0;
    const double done = bytes ? double(m_progress.doneBytes) : double(m_progress.doneFiles);
    const double total = bytes ? double(m_progress.totalBytes) : double(m_progress.totalFiles);
    const qint64 elapsedMs = m_elapsed.elapsed();
    if (done <= 0 || elapsedMs < 1000 || total <= done)
        return -1;
    return qint64((total - done) / (done / double(elapsedMs)) / 1000.0);
}

Job* JobManager::start(
    TransferKind kind, QList<TransferItem> items, QString description, Job::Asker ask, FinishedCallback onFinished)
{
    auto owned = std::make_unique<Job>(kind, std::move(items), std::move(description), std::move(ask));
    Job* job = owned.get();
    connect(job, &Job::finished, this, [this, job, onFinished = std::move(onFinished)] {
        if (onFinished)
            onFinished(job->result());
        // Not from inside the job's own signal; right after it.
        QMetaObject::invokeMethod(
            this,
            [this, job] {
                std::erase_if(m_jobs, [&](const auto& owned) { return owned.get() == job; });
                emit changed();
            },
            Qt::QueuedConnection);
    });
    m_jobs.push_back(std::move(owned));
    job->start();
    emit changed();
    return job;
}

Job* JobManager::run(QString description, Job::Work work, std::function<void(bool cancelled)> onFinished)
{
    auto owned = std::make_unique<Job>(std::move(description), std::move(work));
    Job* job = owned.get();
    connect(job, &Job::finished, this, [this, job, onFinished = std::move(onFinished)] {
        if (onFinished)
            onFinished(job->result().cancelled);
        QMetaObject::invokeMethod(
            this,
            [this, job] {
                std::erase_if(m_jobs, [&](const auto& owned) { return owned.get() == job; });
                emit changed();
            },
            Qt::QueuedConnection);
    });
    m_jobs.push_back(std::move(owned));
    job->start();
    emit changed();
    return job;
}

std::vector<Job*> JobManager::jobs() const
{
    std::vector<Job*> jobs;
    for (const auto& job : m_jobs)
        jobs.push_back(job.get());
    return jobs;
}

} // namespace ariadne
