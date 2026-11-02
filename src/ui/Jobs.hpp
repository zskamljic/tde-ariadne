#pragma once

#include "core/Transfer.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace ariadne {

// Work running on its own thread: a copy or a move, or anything else that says how far it got,
// such as deleting or extracting. Progress and the end arrive as signals on the thread that
// created the job; a transfer's conflicts are put to `ask`, which runs there as well
// (typically showing a dialog) while the transfer waits.
class Job : public QObject {
    Q_OBJECT

public:
    using Asker = std::function<ConflictAnswer(const Conflict&)>;
    // Runs on the job's thread; what it brings about it keeps itself, for when it is done.
    using Work = std::function<void(const JobControl&)>;

    Job(TransferKind kind, QList<TransferItem> items, QString description, Asker ask);
    Job(QString description, Work work);
    ~Job() override; // cancels and waits for the thread

    void start();
    void cancel();
    bool isCancelled() const { return m_cancelled; }

    const QString& description() const { return m_description; }
    const TransferProgress& progress() const { return m_progress; }
    // Rough estimate from the speed so far; negative while unknown.
    qint64 secondsLeft() const;
    bool isFinished() const { return m_finished; }
    const TransferResult& result() const { return m_result; } // of a transfer

signals:
    void progressChanged();
    void finished();

private:
    // Hands `progress` over to this object's thread, not more often than the eye follows.
    void report(const TransferProgress& progress);

    std::unique_ptr<Transfer> m_transfer; // none for other work
    Work m_work;
    QString m_description;
    Asker m_ask;
    TransferProgress m_progress;
    TransferResult m_result;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_lastReport; // on the worker's thread
    std::atomic<bool> m_cancelled = false;
    bool m_finished = false;
    std::jthread m_worker; // last, so it is joined before anything it uses goes away
};

// The running jobs of the application, shown in every window.
class JobManager : public QObject {
    Q_OBJECT

public:
    using FinishedCallback = std::function<void(const TransferResult&)>;

    Job* start(
        TransferKind kind, QList<TransferItem> items, QString description, Job::Asker ask, FinishedCallback onFinished);
    // Runs `work` as a job, then `onFinished` on this thread, told whether it was cancelled.
    Job* run(QString description, Job::Work work, std::function<void(bool cancelled)> onFinished);

    std::vector<Job*> jobs() const;
    bool isEmpty() const { return m_jobs.empty(); }

signals:
    // A job was added or removed.
    void changed();

private:
    std::vector<std::unique_ptr<Job>> m_jobs;
};

} // namespace ariadne
