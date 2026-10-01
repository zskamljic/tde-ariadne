#include "ClipboardFormat.hpp"

#include <QDir>
#include <QMimeData>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace ariadne::clipboard {
namespace {

const QString GnomeFormat = u"x-special/gnome-copied-files"_s;
const QString KdeCutFormat = u"application/x-kde-cutselection"_s;

} // namespace

std::unique_ptr<QMimeData> encode(const Files& files)
{
    auto data = std::make_unique<QMimeData>();
    QList<QUrl> urls;
    QByteArray gnome = files.cut ? "cut" : "copy";
    for (const QString& path : files.paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls << url;
        gnome += '\n' + url.toEncoded();
    }
    data->setUrls(urls);
    data->setData(GnomeFormat, gnome);
    data->setData(KdeCutFormat, files.cut ? "1" : "0");
    data->setText(files.paths.join(u'\n'));
    return data;
}

std::optional<Files> decode(const QMimeData* data)
{
    if (!data)
        return std::nullopt;

    Files files;
    QList<QUrl> urls;
    if (data->hasFormat(GnomeFormat)) {
        const QList<QByteArray> lines = data->data(GnomeFormat).split('\n');
        files.cut = lines.value(0).trimmed() == "cut";
        for (qsizetype i = 1; i < lines.size(); ++i) {
            if (!lines[i].trimmed().isEmpty())
                urls << QUrl::fromEncoded(lines[i].trimmed());
        }
    } else {
        urls = data->urls();
        files.cut = data->data(KdeCutFormat).trimmed() == "1";
    }

    for (const QUrl& url : std::as_const(urls)) {
        if (url.isLocalFile())
            files.paths << QDir::cleanPath(url.toLocalFile());
    }
    if (files.paths.isEmpty())
        return std::nullopt;
    return files;
}

} // namespace ariadne::clipboard
