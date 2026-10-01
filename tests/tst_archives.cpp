#include "core/Archives.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestArchives : public QObject {
    Q_OBJECT

private:
    // Packs the files under `root` (relative names) into `archive` with bsdtar.
    void pack(const QString& archive, const QString& root, const QStringList& names)
    {
        QProcess process;
        process.setWorkingDirectory(root);
        process.start(QStandardPaths::findExecutable(u"bsdtar"_s), QStringList {u"-a"_s, u"-cf"_s, archive} + names);
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 0);
    }

    void write(const QString& path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
    }

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        if (!archives::isAvailable())
            QSKIP("bsdtar is not installed");
        write(m_dir.filePath(u"src/project/readme.txt"_s));
        write(m_dir.filePath(u"src/project/code/main.cpp"_s));
        write(m_dir.filePath(u"src/loose1.txt"_s));
        write(m_dir.filePath(u"src/loose2.txt"_s));
    }

    void recognisesArchives()
    {
        QVERIFY(archives::isArchive(u"application/zip"_s));
        QVERIFY(archives::isArchive(u"application/x-compressed-tar"_s));
        QVERIFY(archives::isArchive(u"application/x-7z-compressed"_s));
        QVERIFY(!archives::isArchive(u"text/plain"_s));
        QCOMPARE(archives::baseName(u"/x/photos.tar.gz"_s), u"photos"_s);
        QCOMPARE(archives::baseName(u"/x/Game v1.2.zip"_s), u"Game v1.2"_s);
    }

    void singleFolderGoesStraightIn()
    {
        const QString archive = m_dir.filePath(u"project.zip"_s);
        pack(archive, m_dir.filePath(u"src"_s), {u"project"_s});
        const QString out = m_dir.filePath(u"out1"_s);
        QDir().mkpath(out);

        const auto result = archives::extract(archive, out);
        if (!result)
            QFAIL(qPrintable(result.error()));
        QCOMPARE(*result, out + u"/project"_s);
        QVERIFY(QFileInfo::exists(out + u"/project/code/main.cpp"_s));

        // A second time the name is taken, so it gets a folder of its own.
        const auto again = archives::extract(archive, out);
        if (!again)
            QFAIL(qPrintable(again.error()));
        QCOMPARE(*again, out + u"/project (2)"_s);
        QVERIFY(QFileInfo::exists(out + u"/project (2)/project/readme.txt"_s));
    }

    void severalItemsGetAFolder()
    {
        const QString archive = m_dir.filePath(u"loose.tar.gz"_s);
        pack(archive, m_dir.filePath(u"src"_s), {u"loose1.txt"_s, u"loose2.txt"_s});
        const QString out = m_dir.filePath(u"out2"_s);
        QDir().mkpath(out);

        const auto result = archives::extract(archive, out);
        if (!result)
            QFAIL(qPrintable(result.error()));
        QCOMPARE(*result, out + u"/loose"_s);
        QVERIFY(QFileInfo::exists(out + u"/loose/loose1.txt"_s));
        QVERIFY(QFileInfo::exists(out + u"/loose/loose2.txt"_s));
    }

    void compressesAndExtractsAgain()
    {
        const QString folder = m_dir.filePath(u"src/project"_s);
        const QString loose = m_dir.filePath(u"src/loose1.txt"_s);
        for (const QString& format : archives::compressFormats()) {
            const QString archive = m_dir.filePath(u"packed."_s + format);
            const auto packed = archives::compress({folder, loose}, archive);
            if (!packed)
                QFAIL(qPrintable(format + u": "_s + packed.error()));
            QVERIFY(!archives::compress({loose}, archive).has_value()); // never overwrites

            const QString out = m_dir.filePath(u"unpacked-"_s + format);
            QDir().mkpath(out);
            const auto extracted = archives::extract(archive, out);
            if (!extracted)
                QFAIL(qPrintable(format + u": "_s + extracted.error()));
            QCOMPARE(*extracted, out + u"/packed"_s); // two items, so a folder of their own
            QVERIFY(QFileInfo::exists(out + u"/packed/project/code/main.cpp"_s));
            QVERIFY(QFileInfo::exists(out + u"/packed/loose1.txt"_s));
        }
    }

    void brokenArchivesFail()
    {
        const QString archive = m_dir.filePath(u"broken.zip"_s);
        write(archive);
        const QString out = m_dir.filePath(u"out3"_s);
        QDir().mkpath(out);
        const auto result = archives::extract(archive, out);
        QVERIFY(!result.has_value());
        QVERIFY(!result.error().isEmpty());
        QVERIFY(QDir(out).isEmpty()); // nothing left behind
    }
};

QTEST_GUILESS_MAIN(TestArchives)
#include "tst_archives.moc"
