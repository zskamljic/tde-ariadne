#pragma once

#include "core/Transfer.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace ariadne {

// A copy or move running on its own thread. Progress and the result arrive as signals on
// the thread that created the job; conflicts are put to `ask`, which runs there as well
// (typically showing a dialog) while the transfer waits.
class Job : public QObject {
    Q_OBJECT

public:
    using Asker = std::function<ConflictAnswer(const Conflict&)>;

    Job(TransferKind kind, QList<TransferItem> items, QString description, Asker ask);
    ~Job() override; // cancels and waits for the thread

    void start();
    void cancel() { m_transfer.cancel(); }

    TransferKind kind() const { return m_transfer.kind(); }
    const QString& description() const { return m_description; }
    const TransferProgress& progress() const { return m_progress; }
    // Rough estimate from the speed so far; negative while unknown.
    qint64 secondsLeft() const;
    bool isFinished() const { return m_finished; }
    const TransferResult& result() const { return m_result; }

signals:
    void progressChanged();
    void finished();

private:
    Transfer m_transfer;
    QString m_description;
    Asker m_ask;
    TransferProgress m_progress;
    TransferResult m_result;
    QElapsedTimer m_elapsed;
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

    std::vector<Job*> jobs() const;
    bool isEmpty() const { return m_jobs.empty(); }

signals:
    // A job was added or removed.
    void changed();

private:
    std::vector<std::unique_ptr<Job>> m_jobs;
};

} // namespace ariadne
