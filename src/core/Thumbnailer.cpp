#include "Thumbnailer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr int SizeClasses[] = {128, 256, 512, 1024};
constexpr qint64 MaxFileSize = 200LL * 1024 * 1024;
constexpr int ExternalTimeoutMs = 15'000;

// Where failed attempts are remembered, so they are not retried on every visit.
QString failPath(const QString& uri)
{
    return thumbnails::cacheDirectory() + u"/fail/ariadne/"_s
        + QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex()) + u".png"_s;
}

QString mtimeText(const QDateTime& modified)
{
    return QString::number(modified.toSecsSinceEpoch());
}

// A cached image is valid if it was made from the file as it is now.
QImage readCached(const QString& path, const QString& mtime)
{
    QImageReader reader(path, "png");
    if (!reader.canRead() || reader.text(u"Thumb::MTime"_s) != mtime)
        return {};
    return reader.read();
}

bool store(QImage image, const QString& path, const QString& uri, const ThumbnailRequest& request)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory))
        return false;
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    image.setText(u"Thumb::URI"_s, uri);
    image.setText(u"Thumb::MTime"_s, mtimeText(request.modified));
    image.setText(u"Thumb::Size"_s, QString::number(request.size));
    image.setText(u"Thumb::Mimetype"_s, request.mimeType);
    image.setText(u"Software"_s, u"Ariadne"_s);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "png") || !file.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QImage scaledToFit(const QImage& image, int pixels)
{
    if (image.width() <= pixels && image.height() <= pixels)
        return image;
    return image.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage readImage(const QString& path, int pixels)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid() && (original.width() > pixels || original.height() > pixels))
        reader.setScaledSize(original.scaled(pixels, pixels, Qt::KeepAspectRatio));
    return reader.read();
}

// Replaces the % codes of a thumbnailer's Exec line in one pass, so substituted text is
// never expanded again.
QString expandField(const QString& argument, const QMap<QChar, QString>& values)
{
    QString result;
    for (qsizetype i = 0; i < argument.size(); ++i) {
        if (argument[i] == u'%' && i + 1 < argument.size()) {
            const QChar code = argument[++i];
            result += code == u'%' ? u"%"_s : values.value(code);
        } else {
            result += argument[i];
        }
    }
    return result;
}

QImage runExternal(const ExternalThumbnailer& thumbnailer, const ThumbnailRequest& request, int pixels)
{
    const QTemporaryDir directory;
    if (!directory.isValid())
        return {};
    const QString output = directory.filePath(u"thumbnail.png"_s);
    const QMap<QChar, QString> values {
        {u's', QString::number(pixels)},
        {u'u', thumbnails::fileUri(request.path)},
        {u'i', request.path},
        {u'o', output},
    };

    QStringList arguments = QProcess::splitCommand(thumbnailer.exec);
    if (arguments.isEmpty())
        return {};
    for (QString& argument : arguments)
        argument = expandField(argument, values);

    QProcess process;
    process.setProgram(arguments.takeFirst());
    process.setArguments(arguments);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForFinished(ExternalTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return scaledToFit(QImage(output), pixels);
}

const ExternalThumbnailer* findFor(const QString& mimeType, const QList<ExternalThumbnailer>& external)
{
    static const QMimeDatabase database;
    QStringList candidates {mimeType};
    const QMimeType mime = database.mimeTypeForName(mimeType);
    candidates << mime.aliases() << mime.allAncestors();
    for (const QString& candidate : std::as_const(candidates)) {
        for (const ExternalThumbnailer& thumbnailer : external) {
            if (thumbnailer.mimeTypes.contains(candidate))
                return &thumbnailer;
        }
    }
    return nullptr;
}

std::optional<ExternalThumbnailer> parseThumbnailerFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    ExternalThumbnailer thumbnailer;
    QString tryExec;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == u"[Thumbnailer Entry]";
            continue;
        }
        const qsizetype equals = line.indexOf(u'=');
        if (!inEntry || line.startsWith(u'#') || equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == u"Exec")
            thumbnailer.exec = value;
        else if (key == u"TryExec")
            tryExec = value;
        else if (key == u"MimeType")
            thumbnailer.mimeTypes = value.split(u';', Qt::SkipEmptyParts);
    }

    if (thumbnailer.exec.isEmpty() || thumbnailer.mimeTypes.isEmpty())
        return std::nullopt;
    if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty() && !QFileInfo(tryExec).isExecutable())
        return std::nullopt;
    return thumbnailer;
}

} // namespace

namespace thumbnails {

QString cacheDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + u"/thumbnails"_s;
}

QString sizeDirectory(int pixels)
{
    if (pixels <= 128)
        return u"normal"_s;
    if (pixels <= 256)
        return u"large"_s;
    if (pixels <= 512)
        return u"x-large"_s;
    return u"xx-large"_s;
}

QString fileUri(const QString& path)
{
    return QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
}

QString cachePath(const QString& path, int pixels)
{
    const QByteArray hash = QCryptographicHash::hash(fileUri(path).toUtf8(), QCryptographicHash::Md5).toHex();
    return u"%1/%2/%3.png"_s.arg(cacheDirectory(), sizeDirectory(pixels), QString::fromLatin1(hash));
}

QList<ExternalThumbnailer> findExternalThumbnailers()
{
    QList<ExternalThumbnailer> result;
    QStringList seen;
    const QStringList directories = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, u"thumbnailers"_s, QStandardPaths::LocateDirectory);
    // Earlier directories (the user's own) take precedence over later ones.
    for (const QString& directory : directories) {
        for (const auto& entry : QDirListing(directory, {u"*.thumbnailer"_s})) {
            if (seen.contains(entry.fileName()))
                continue;
            seen << entry.fileName();
            if (auto thumbnailer = parseThumbnailerFile(entry.absoluteFilePath()))
                result << *thumbnailer;
        }
    }
    return result;
}

QImage load(const ThumbnailRequest& request, int pixels, const QList<ExternalThumbnailer>& external)
{
    const QString uri = fileUri(request.path);
    const QString mtime = mtimeText(request.modified);

    // Any cached size at least as large as wanted will do.
    for (const int size : SizeClasses) {
        if (size < pixels)
            continue;
        const QImage cached = readCached(cachePath(request.path, size), mtime);
        if (!cached.isNull())
            return scaledToFit(cached, pixels);
    }
    if (!readCached(failPath(uri), mtime).isNull())
        return {};

    QImage image;
    if (request.mimeType.startsWith(u"image/")
        && QImageReader::supportedMimeTypes().contains(request.mimeType.toUtf8()))
        image = readImage(request.path, pixels);
    if (image.isNull()) {
        if (const ExternalThumbnailer* thumbnailer = findFor(request.mimeType, external))
            image = runExternal(*thumbnailer, request, pixels);
    }

    if (image.isNull()) {
        QImage marker(1, 1, QImage::Format_ARGB32);
        marker.fill(Qt::transparent);
        store(marker, failPath(uri), uri, request);
        return {};
    }
    image = image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    store(image, cachePath(request.path, pixels), uri, request);
    return image;
}

} // namespace thumbnails

Thumbnailer::Thumbnailer(QObject* parent)
    : QObject(parent)
    , m_external(std::make_shared<const QList<ExternalThumbnailer>>(thumbnails::findExternalThumbnailers()))
{
    for (const QByteArray& type : QImageReader::supportedMimeTypes())
        m_imageMimeTypes << QString::fromLatin1(type);
    m_pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
}

Thumbnailer::~Thumbnailer()
{
    cancelPending();
    m_pool.waitForDone();
}

void Thumbnailer::setSize(int pixels)
{
    const auto it = std::ranges::find_if(SizeClasses, [&](int size) { return size >= pixels; });
    m_size = it == std::end(SizeClasses) ? SizeClasses[std::size(SizeClasses) - 1] : *it;
}

bool Thumbnailer::canThumbnail(const QString& path, const QString& mimeType, qint64 fileSize) const
{
    if (fileSize <= 0 || fileSize > MaxFileSize || path.startsWith(thumbnails::cacheDirectory() + u'/'))
        return false;
    if (m_imageMimeTypes.contains(mimeType))
        return true;
    return findFor(mimeType, *m_external) != nullptr;
}

void Thumbnailer::request(const ThumbnailRequest& request)
{
    const quint64 generation = m_generation;
    m_pool.start([this, request, generation, size = m_size, external = m_external] {
        if (generation != m_generation)
            return;
        const QImage image = thumbnails::load(request, size, *external);
        QMetaObject::invokeMethod(
            this,
            [this, path = request.path, image, generation] {
                if (generation != m_generation)
                    return;
                if (image.isNull())
                    emit failed(path);
                else
                    emit ready(path, image);
            },
            Qt::QueuedConnection);
    });
}

void Thumbnailer::cancelPending()
{
    ++m_generation;
    m_pool.clear();
}

} // namespace ariadne
