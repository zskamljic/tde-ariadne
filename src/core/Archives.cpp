#include "Archives.hpp"

#include "FileOperations.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

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

} // namespace ariadne::archives
