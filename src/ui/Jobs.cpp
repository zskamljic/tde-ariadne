#include "Jobs.hpp"

#include <chrono>
#include <future>

using namespace std::chrono_literals;

namespace ariadne {

Job::Job(TransferKind kind, QList<TransferItem> items, QString description, Asker ask)
    : m_transfer(kind, std::move(items))
    , m_description(std::move(description))
    , m_ask(std::move(ask))
{
}

Job::~Job()
{
    m_transfer.cancel();
}

void Job::start()
{
    m_elapsed.start();

    // Called on the worker thread; hands the numbers over to this object's thread.
    m_transfer.setProgressCallback([this](const TransferProgress& progress) {
        QMetaObject::invokeMethod(
            this,
            [this, progress] {
                m_progress = progress;
                emit progressChanged();
            },
            Qt::QueuedConnection);
    });

    // The worker asks on this object's thread and waits for the answer, giving up when the
    // job is cancelled meanwhile, so it can never block anyone waiting for it to finish.
    m_transfer.setConflictCallback([this, ask = m_ask](const Conflict& conflict) {
        auto promise = std::make_shared<std::promise<ConflictAnswer>>();
        std::future<ConflictAnswer> answer = promise->get_future();
        QMetaObject::invokeMethod(
            this, [promise, ask, conflict] { promise->set_value(ask ? ask(conflict) : ConflictAnswer {}); },
            Qt::QueuedConnection);
        while (answer.wait_for(50ms) != std::future_status::ready) {
            if (m_transfer.isCancelled())
                return ConflictAnswer {ConflictChoice::Cancel, false};
        }
        return answer.get();
    });

    m_worker = std::jthread([this] {
        TransferResult result = m_transfer.run();
        QMetaObject::invokeMethod(
            this,
            [this, result = std::move(result)] {
                m_result = result;
                m_finished = true;
                emit finished();
            },
            Qt::QueuedConnection);
    });
}

qint64 Job::secondsLeft() const
{
    const qint64 elapsedMs = m_elapsed.elapsed();
    if (m_progress.doneBytes <= 0 || elapsedMs < 1000 || m_progress.totalBytes <= m_progress.doneBytes)
        return -1;
    const double bytesPerMs = double(m_progress.doneBytes) / double(elapsedMs);
    return qint64(double(m_progress.totalBytes - m_progress.doneBytes) / bytesPerMs / 1000.0);
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

std::vector<Job*> JobManager::jobs() const
{
    std::vector<Job*> jobs;
    for (const auto& job : m_jobs)
        jobs.push_back(job.get());
    return jobs;
}

} // namespace ariadne
