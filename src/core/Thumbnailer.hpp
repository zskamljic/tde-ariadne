#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace ariadne {

// A program that renders thumbnails for some MIME types, from a .thumbnailer file.
struct ExternalThumbnailer {
    QString exec; // command line with %s (size), %u (URI), %i (path) and %o (output file)
    QStringList mimeTypes;
};

struct ThumbnailRequest {
    QString path;
    QString mimeType;
    QDateTime modified;
    qint64 size = 0;
};

// Thumbnails following the freedesktop.org thumbnail spec, so the cache in
// ~/.cache/thumbnails is shared with other file managers. Images are scaled in-process;
// other types go through the thumbnailers installed in <data dir>/thumbnailers.
class Thumbnailer : public QObject {
    Q_OBJECT

public:
    explicit Thumbnailer(QObject* parent = nullptr);
    ~Thumbnailer() override;

    // Rounded up to one of the spec's sizes: 128, 256, 512 or 1024 pixels.
    int size() const { return m_size; }
    void setSize(int pixels);

    bool canThumbnail(const QString& path, const QString& mimeType, qint64 fileSize) const;
    void request(const ThumbnailRequest& request);
    // Drops queued requests, for example after leaving a folder.
    void cancelPending();

signals:
    void ready(const QString& path, const QImage& image);
    void failed(const QString& path);

private:
    std::shared_ptr<const QList<ExternalThumbnailer>> m_external;
    QStringList m_imageMimeTypes;
    QThreadPool m_pool;
    std::atomic<quint64> m_generation = 0;
    int m_size = 256;
};

namespace thumbnails {

QString cacheDirectory();
// The spec's size class for `pixels`: "normal", "large", "x-large" or "xx-large".
QString sizeDirectory(int pixels);
QString fileUri(const QString& path);
QString cachePath(const QString& path, int pixels);

QList<ExternalThumbnailer> findExternalThumbnailers();

// Returns a valid cached thumbnail, or creates and caches one. Blocking.
QImage load(const ThumbnailRequest& request, int pixels, const QList<ExternalThumbnailer>& external);

} // namespace thumbnails

} // namespace ariadne
