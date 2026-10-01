#pragma once

#include "FileOperations.hpp"

#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace ariadne {

enum class TransferKind { Copy, Move };

struct TransferItem {
    QString source;
    QString destination;

    bool operator==(const TransferItem&) const = default;
};

// Something already exists where an item would go.
struct Conflict {
    QString source;
    QString destination;
    bool folders = false; // both are folders, so they can be merged
};

enum class ConflictChoice { Replace, Merge, Skip, KeepBoth, Cancel };

struct ConflictAnswer {
    ConflictChoice choice = ConflictChoice::Skip;
    bool applyToAll = false; // answer every later conflict of the same kind the same way
};

struct TransferProgress {
    qint64 doneBytes = 0;
    qint64 totalBytes = 0;
    int doneFiles = 0;
    int totalFiles = 0;
    QString current; // name of the file being worked on
};

struct TransferResult {
    QList<fileops::Failure> failures;
    QList<TransferItem> completed; // with the destinations the items really ended up at
    bool cancelled = false;
};

// Copies or moves files and folders. Moving within a file system is a rename; otherwise
// items are copied, keeping permissions, modification times and symbolic links, and moved
// items are removed afterwards. run() blocks, so it belongs on a worker thread; cancel()
// may be called from any thread.
class Transfer {
public:
    using ProgressCallback = std::function<void(const TransferProgress&)>;
    using ConflictCallback = std::function<ConflictAnswer(const Conflict&)>;

    Transfer(TransferKind kind, QList<TransferItem> items);

    // Items that put `sources` into `directory` under their own names.
    static QList<TransferItem> into(const QStringList& sources, const QString& directory);

    TransferKind kind() const { return m_kind; }
    const QList<TransferItem>& items() const { return m_items; }

    // Called at most every `intervalMs` while working, and at the start and end.
    void setProgressCallback(ProgressCallback callback, int intervalMs = 100)
    {
        m_onProgress = std::move(callback);
        m_reportIntervalMs = intervalMs;
    }
    // Without one, conflicts are skipped.
    void setConflictCallback(ConflictCallback callback) { m_onConflict = std::move(callback); }

    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

    TransferResult run();

private:
    struct Size {
        qint64 bytes = 0;
        int files = 0;
    };

    Size measure(const QString& path) const;
    void transfer(const TransferItem& item, const Size& size);
    bool copyEntry(const QString& source, const QString& destination);
    bool copyFile(const QString& source, const QString& destination);
    std::optional<ConflictAnswer> resolve(const Conflict& conflict);
    void skip(const Size& size);
    void fail(const QString& path, const QString& reason);
    void report(bool force = false);

    TransferKind m_kind;
    QList<TransferItem> m_items;
    ProgressCallback m_onProgress;
    int m_reportIntervalMs = 100;
    ConflictCallback m_onConflict;
    std::atomic<bool> m_cancelled = false;

    TransferProgress m_progress;
    TransferResult m_result;
    QElapsedTimer m_sinceReport;
    std::optional<ConflictAnswer> m_forAllFiles;
    std::optional<ConflictAnswer> m_forAllFolders;
};

} // namespace ariadne
