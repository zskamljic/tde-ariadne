#include "Search.hpp"

#include "Location.hpp"

#include <QDirListing>
#include <QElapsedTimer>
#include <QStandardPaths>

#include <deque>

using namespace Qt::StringLiterals;

namespace ariadne::search {
namespace {

// Lower case without accents, for comparing names the way people type them.
QString simplified(const QString& text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString result;
    result.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            result += c;
    }
    return result.toCaseFolded();
}

} // namespace

bool matches(const QString& name, const QString& query)
{
    const QString haystack = simplified(name);
    const QStringList words = simplified(query).split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
        return false;
    for (const QString& word : words) {
        if (!haystack.contains(word))
            return false;
    }
    return true;
}

void run(const QUrl& root, const Options& options, std::stop_token stop,
    const std::function<void(std::vector<FileEntry>)>& found)
{
    const QMimeDatabase mimeDatabase;
    const bool remote = isRemoteFilesystem(location::localPath(root));
    // Phones and network locations are not searched unless the search starts in one.
    const QString gvfsFolder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + u"/gvfs"_s;
    std::vector<FileEntry> batch;
    int total = 0;
    QElapsedTimer sinceBatch;
    sinceBatch.start();
    const auto flush = [&] {
        if (!batch.empty())
            found(std::exchange(batch, {}));
        sinceBatch.restart();
    };

    // Breadth first, so results close to where the search started come first.
    std::deque<QUrl> folders {root};
    while (!folders.empty() && !stop.stop_requested()) {
        const QUrl folder = folders.front();
        folders.pop_front();
        const QString path = location::localPath(folder);
        const QSet<QString> hiddenNames = readHiddenFile(path);

        for (const auto& item : QDirListing(path, QDirListing::IteratorFlag::IncludeHidden)) {
            if (stop.stop_requested())
                return;
            const QString name = item.fileName();
            const bool hidden = name.startsWith(u'.') || hiddenNames.contains(name);
            if (hidden && !options.includeHidden)
                continue;
            const QUrl url = location::child(folder, name);
            if (item.isDir() && !item.isSymLink() && (remote || item.absoluteFilePath() != gvfsFolder))
                folders.push_back(url);
            if (!matches(name, options.query))
                continue;
            batch.push_back(describeEntry(item.fileInfo(), url, hidden, mimeDatabase, remote));
            if (++total >= options.maxResults) {
                flush();
                return;
            }
            if (batch.size() >= 200 || sinceBatch.elapsed() > 150)
                flush();
        }
        if (sinceBatch.elapsed() > 150)
            flush();
    }
    flush();
}

} // namespace ariadne::search
