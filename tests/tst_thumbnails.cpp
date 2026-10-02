#include "core/Thumbnailer.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestThumbnails : public QObject {
    Q_OBJECT

    static int runSandboxed(const QString& script, const QString& input, const QString& output)
    {
        QStringList command = thumbnails::sandboxed({u"sh"_s, u"-c"_s, script}, input, u"/tmp/input.txt"_s, output);
        QProcess process;
        process.start(command.takeFirst(), command);
        return process.waitForFinished(10'000) ? process.exitCode() : -1;
    }

    QTemporaryDir m_cache;
    QTemporaryDir m_data;

private slots:
    void initTestCase()
    {
        // Thumbnails made here are not the user's to keep.
        qputenv("XDG_CACHE_HOME", m_cache.path().toLocal8Bit());
        // A thumbnailer of our own, for a type nothing else handles.
        qputenv("XDG_DATA_HOME", m_data.path().toLocal8Bit());
        QVERIFY(QDir().mkpath(m_data.filePath(u"thumbnailers"_s)));
        QFile thumbnailer(m_data.filePath(u"thumbnailers/test.thumbnailer"_s));
        QVERIFY(thumbnailer.open(QIODevice::WriteOnly));
        thumbnailer.write("[Thumbnailer Entry]\nExec=true %i %o\nMimeType=video/x-ariadne-test;\n");
    }

    void largeFilesGoToThumbnailers()
    {
        // Videos are often larger than any image decoded here; their thumbnailers take them.
        const Thumbnailer thumbnailer;
        constexpr qint64 large = 2LL * 1024 * 1024 * 1024;
        QVERIFY(thumbnailer.canThumbnail(u"/videos/film.test"_s, u"video/x-ariadne-test"_s, large));
        QVERIFY(thumbnailer.canThumbnail(u"/videos/clip.test"_s, u"video/x-ariadne-test"_s, 1024));
        QVERIFY(!thumbnailer.canThumbnail(u"/videos/empty.test"_s, u"video/x-ariadne-test"_s, 0));
        QVERIFY(!thumbnailer.canThumbnail(u"/data/blob.bin"_s, u"application/x-ariadne-none"_s, 1024));
    }

    void sandboxIsolates()
    {
        if (!thumbnails::canSandbox())
            QSKIP("bubblewrap is not available here");
        QTemporaryDir home;
        QTemporaryDir output;
        const QString input = home.filePath(u"secret.txt"_s);
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("visible");
        file.close();

        // The file to thumbnail is there, at the path given, and the output folder is writable.
        QCOMPARE(runSandboxed(
                     u"grep -q visible /tmp/input.txt && touch '%1/out'"_s.arg(output.path()), input, output.path()),
            0);
        QVERIFY(QFile::exists(output.filePath(u"out"_s)));
        // Nothing else of the user's: not the home folder, nor the folder the file came from.
        QCOMPARE(runSandboxed(u"test ! -e /home && test ! -e '%1'"_s.arg(home.path()), input, output.path()), 0);
        // The system cannot be written to.
        QVERIFY(runSandboxed(u"touch /usr/ariadne-test"_s, input, output.path()) != 0);
    }

    void thumbnailsInSandbox()
    {
        if (!thumbnails::canSandbox())
            QSKIP("bubblewrap is not available here");
        // A font, as most systems have both one and the font thumbnailer.
        const auto external = thumbnails::findExternalThumbnailers();
        QString font;
        for (QDirIterator it(u"/usr/share/fonts"_s, {u"*.ttf"_s}, QDir::Files, QDirIterator::Subdirectories);
            it.hasNext() && font.isEmpty();)
            font = it.next();
        const bool fontThumbnailer = std::ranges::any_of(
            external, [](const ExternalThumbnailer& t) { return t.mimeTypes.contains(u"font/ttf"_s); });
        if (font.isEmpty() || !fontThumbnailer)
            QSKIP("no TrueType font or font thumbnailer here");

        const QFileInfo info(font);
        const QImage image = thumbnails::load({font, u"font/ttf"_s, info.lastModified(), info.size()}, 128, external);
        QVERIFY(!image.isNull());
        QVERIFY(image.width() <= 128 && image.height() <= 128);
    }
};

QTEST_GUILESS_MAIN(TestThumbnails)
#include "tst_thumbnails.moc"
