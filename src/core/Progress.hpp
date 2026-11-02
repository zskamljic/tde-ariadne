#pragma once

#include <QString>

#include <functional>

namespace ariadne {

struct TransferProgress {
    qint64 doneBytes = 0;
    qint64 totalBytes = 0;
    int doneFiles = 0;
    int totalFiles = 0;
    QString current; // name of the file being worked on
};

// What work running on another thread is handed: where to say how far it got, and how to hear
// that it should stop. A default one hears nothing and is never stopped.
class JobControl {
public:
    JobControl() = default;
    JobControl(std::function<void(const TransferProgress&)> report, std::function<bool()> cancelled)
        : m_report(std::move(report))
        , m_cancelled(std::move(cancelled))
    {
    }

    void report(const TransferProgress& progress) const
    {
        if (m_report)
            m_report(progress);
    }
    bool cancelled() const { return m_cancelled && m_cancelled(); }

private:
    std::function<void(const TransferProgress&)> m_report;
    std::function<bool()> m_cancelled;
};

} // namespace ariadne
