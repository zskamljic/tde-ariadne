#include "Archives.hpp"

#include "FileOperations.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <deque>
#include <mutex>

using namespace Qt::StringLiterals;

namespace ariadne::archives {
namespace {

const QStringList archiveTypes {
    u"application/zip"_s,
    u"application/x-7z-compressed"_s,
    u"application/vnd.rar"_s,
    u"application/x-rar"_s,
    u"application/x-tar"_s,
    u"application/x-compressed-tar"_s,
    u"application/x-bzip-compressed-tar"_s,
    u"application/x-bzip2-compressed-tar"_s,
    u"application/x-xz-compressed-tar"_s,
    u"application/x-zstd-compressed-tar"_s,
    u"application/x-lzma-compressed-tar"_s,
    u"application/x-lz4-compressed-tar"_s,
    u"application/x-cpio"_s,
    u"application/x-iso9660-image"_s,
    u"application/x-cd-image"_s,
    u"application/vnd.ms-cab-compressed"_s,
    u"application/x-archive"_s,
    u"application/x-xar"_s,
    u"application/x-lha"_s,
};

QString bsdtar()
{
    return QStandardPaths::findExecutable(u"bsdtar"_s);
}

struct Result {
    bool ok;
    QString output;
    QString error;
};

Result run(const QStringList& arguments)
{
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start(bsdtar(), arguments);
    if (!process.waitForStarted())
        return {false, {}, process.errorString()};
    process.waitForFinished(-1);
    const QString error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return {ok, QString::fromLocal8Bit(process.readAllStandardOutput()), error};
}

// bsdtar messages start with "bsdtar: "; the rest reads fine on its own.
QString cleaned(const QString& error)
{
    QString text = error.section(u'\n', 0, 0);
    if (text.startsWith(u"bsdtar: "))
        text = text.mid(8);
    return text.isEmpty() ? u"The archive could not be read."_s : text;
}

} // namespace

bool isAvailable()
{
    return !bsdtar().isEmpty();
}

bool isArchive(const QString& mimeType)
{
    if (archiveTypes.contains(mimeType))
        return true;
    const QMimeType mime = QMimeDatabase().mimeTypeForName(mimeType);
    return std::ranges::any_of(archiveTypes, [&](const QString& type) { return mime.inherits(type); });
}

QString baseName(const QString& archivePath)
{
    const QString name = QFileInfo(archivePath).fileName();
    const QString suffix = QMimeDatabase().suffixForFileName(name);
    if (!suffix.isEmpty() && name.size() > suffix.size() + 1)
        return name.chopped(suffix.size() + 1);
    const QString stem = QFileInfo(name).completeBaseName();
    return stem.isEmpty() ? name : stem;
}

QStringList compressFormats()
{
    return {u"zip"_s, u"tar.xz"_s, u"tar.gz"_s, u"tar.zst"_s, u"7z"_s};
}

std::expected<void, QString> compress(const QStringList& paths, const QString& archivePath)
{
    if (!isAvailable())
        return std::unexpected(u"Compressing needs bsdtar, from libarchive."_s);
    if (paths.isEmpty())
        return std::unexpected(u"There is nothing to compress."_s);
    if (QFileInfo(archivePath).exists())
        return std::unexpected(u"“%1” already exists."_s.arg(QFileInfo(archivePath).fileName()));

    // -a picks the format from the extension; each item is added from its own folder.
    QStringList arguments {u"-a"_s, u"-cf"_s, archivePath};
    for (const QString& path : paths) {
        const QFileInfo info(path);
        arguments << u"-C"_s << info.absolutePath() << info.fileName();
    }
    const Result result = run(arguments);
    if (!result.ok) {
        QFile::remove(archivePath);
        return std::unexpected(cleaned(result.error));
    }
    return {};
}

std::expected<QString, QString> extract(const QString& archivePath, const QString& directory)
{
    if (!isAvailable())
        return std::unexpected(u"Extracting archives needs bsdtar, from libarchive."_s);

    const Result listing = run({u"-tf"_s, archivePath});
    if (!listing.ok)
        return std::unexpected(cleaned(listing.error));

    QSet<QString> topLevel;
    for (QString entry : listing.output.split(u'\n', Qt::SkipEmptyParts)) {
        while (entry.startsWith(u"./"))
            entry.remove(0, 2);
        const QString first = entry.section(u'/', 0, 0);
        if (!first.isEmpty() && first != u".")
            topLevel.insert(first);
    }
    if (topLevel.isEmpty())
        return std::unexpected(u"The archive is empty."_s);

    const QDir target(directory);
    QString destination = directory;
    QString result;
    bool createdFolder = false;
    if (const QString single = topLevel.size() == 1 ? *topLevel.cbegin() : QString();
        !single.isEmpty() && !QFileInfo(target.filePath(single)).exists()) {
        result = target.filePath(single);
    } else {
        destination = target.filePath(fileops::uniqueName(directory, baseName(archivePath)));
        if (!QDir().mkdir(destination))
            return std::unexpected(u"The folder “%1” could not be created."_s.arg(destination));
        createdFolder = true;
        result = destination;
    }

    const Result extraction = run({u"-xf"_s, archivePath, u"-C"_s, destination});
    if (!extraction.ok) {
        if (createdFolder && QDir(destination).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            QDir().rmdir(destination);
        return std::unexpected(cleaned(extraction.error));
    }
    return result;
}

namespace {

QString normalized(QString path)
{
    while (path.startsWith(u"./"))
        path.remove(0, 2);
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    while (path.endsWith(u'/'))
        path.chop(1);
    return path == u"." ? QString() : path;
}

std::expected<std::vector<Entry>, QString> read(const QString& archivePath)
{
    const std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), &archive_read_free);
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());
    if (archive_read_open_filename(reader.get(), QFile::encodeName(archivePath).constData(), 64 * 1024) != ARCHIVE_OK)
        return std::unexpected(QString::fromLocal8Bit(archive_error_string(reader.get())));

    std::vector<Entry> entries;
    archive_entry* header = nullptr;
    int status = ARCHIVE_OK;
    while ((status = archive_read_next_header(reader.get(), &header)) == ARCHIVE_OK || status == ARCHIVE_WARN) {
        const char* utf8 = archive_entry_pathname_utf8(header);
        const QString stored = utf8 ? QString::fromUtf8(utf8) : QFile::decodeName(archive_entry_pathname(header));
        Entry entry;
        entry.path = normalized(stored);
        if (entry.path.isEmpty())
            continue;
        entry.stored = stored;
        const auto type = archive_entry_filetype(header);
        entry.isDir = type == AE_IFDIR;
        entry.isSymlink = type == AE_IFLNK;
        entry.size = archive_entry_size_is_set(header) ? archive_entry_size(header) : 0;
        if (archive_entry_mtime_is_set(header))
            entry.modified = QDateTime::fromSecsSinceEpoch(archive_entry_mtime(header));
        entries.push_back(std::move(entry));
        archive_read_data_skip(reader.get());
    }
    if (status != ARCHIVE_EOF)
        return std::unexpected(QString::fromLocal8Bit(archive_error_string(reader.get())));
    return entries;
}

struct Cached {
    QString path;
    qint64 size;
    QDateTime modified;
    std::shared_ptr<const std::vector<Entry>> entries;
};

} // namespace

std::expected<std::shared_ptr<const std::vector<Entry>>, QString> contents(const QString& archivePath)
{
    // Listings are read on worker threads, several at once when folders are expanded.
    static std::mutex mutex;
    static std::deque<Cached> cache;
    const QFileInfo info(archivePath);
    {
        const std::scoped_lock lock(mutex);
        for (const Cached& cached : cache) {
            if (cached.path == archivePath && cached.size == info.size() && cached.modified == info.lastModified())
                return cached.entries;
        }
    }
    auto entries = read(archivePath);
    if (!entries)
        return std::unexpected(entries.error().isEmpty() ? u"The archive could not be read."_s : entries.error());
    auto shared = std::make_shared<const std::vector<Entry>>(std::move(*entries));
    const std::scoped_lock lock(mutex);
    std::erase_if(cache, [&](const Cached& cached) { return cached.path == archivePath; });
    cache.push_front({archivePath, info.size(), info.lastModified(), shared});
    if (cache.size() > 4)
        cache.pop_back();
    return shared;
}

std::expected<std::vector<Entry>, QString> list(const QString& archivePath, const QString& folder)
{
    const auto all = contents(archivePath);
    if (!all)
        return std::unexpected(all.error());
    const QString prefix = folder.isEmpty() ? QString() : folder + u'/';
    std::vector<Entry> children;
    QHash<QString, std::size_t> byName;
    for (const Entry& entry : **all) {
        if (!entry.path.startsWith(prefix))
            continue;
        const QString rest = entry.path.mid(prefix.size());
        const qsizetype slash = rest.indexOf(u'/');
        const QString name = slash < 0 ? rest : rest.left(slash);
        if (name.isEmpty())
            continue;
        if (slash < 0) {
            // The entry itself; it may stand in for a folder already implied by its contents.
            if (const auto it = byName.constFind(name); it != byName.cend())
                children[*it] = entry;
            else {
                byName.insert(name, children.size());
                children.push_back(entry);
            }
        } else if (!byName.contains(name)) {
            Entry implied;
            implied.path = prefix + name;
            implied.isDir = true;
            byName.insert(name, children.size());
            children.push_back(std::move(implied));
        }
    }
    return children;
}

QString stagingFolder()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/unpacked"_s;
    const QDateTime dayAgo = QDateTime::currentDateTime().addDays(-1);
    for (const QFileInfo& old : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (old.lastModified() < dayAgo)
            QDir(old.absoluteFilePath()).removeRecursively();
    }
    const QString folder = root + u'/' + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    QDir().mkpath(folder);
    return folder;
}

std::expected<void, QString> extractPaths(
    const QString& archivePath, const QStringList& paths, const QString& directory)
{
    if (!isAvailable())
        return std::unexpected(u"Extracting archives needs bsdtar, from libarchive."_s);
    if (paths.isEmpty())
        return {};
    const auto all = contents(archivePath);
    if (!all)
        return std::unexpected(all.error());

    // Names as stored; folders only implied are found through what is in them.
    QStringList members;
    int strip = -1;
    for (const QString& path : paths) {
        QString stored;
        for (const Entry& entry : **all) {
            if (entry.path == path && !entry.stored.isEmpty()) {
                stored = entry.stored;
                break;
            }
            if (stored.isEmpty() && entry.path.startsWith(path + u'/')) {
                // Without trailing slashes, the path is the end of the stored name: "./a/b/c.txt"
                // for "a/b/c.txt" makes the implied folder "a/b" "./a/b".
                QString name = entry.stored;
                while (name.endsWith(u'/'))
                    name.chop(1);
                stored = name.left(name.size() - (entry.path.size() - path.size()));
            }
        }
        if (stored.isEmpty())
            return std::unexpected(u"“%1” is not in the archive."_s.arg(path));
        while (stored.endsWith(u'/'))
            stored.chop(1);
        members << stored;
        // The folders above it inside the archive are left out, so it lands right in `directory`.
        if (strip < 0)
            strip = int(stored.count(u'/'));
    }

    QDir().mkpath(directory);
    QStringList arguments {u"-xf"_s, archivePath, u"-C"_s, directory};
    if (strip > 0)
        arguments << u"--strip-components"_s << QString::number(strip);
    const Result result = run(arguments + members);
    if (!result.ok)
        return std::unexpected(cleaned(result.error));
    return {};
}

} // namespace ariadne::archives
