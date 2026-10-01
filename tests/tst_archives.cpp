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

    void listsInside()
    {
        // Folder entries of their own (zip), only files (tar of named files), and "./" names.
        const QString zip = m_dir.filePath(u"browse.zip"_s);
        pack(zip, m_dir.filePath(u"src"_s), {u"project"_s, u"loose1.txt"_s});
        const QString filesOnly = m_dir.filePath(u"files-only.tar"_s);
        pack(filesOnly, m_dir.filePath(u"src"_s), {u"project/code/main.cpp"_s, u"loose2.txt"_s});
        const QString dotted = m_dir.filePath(u"dotted.tar.gz"_s);
        pack(dotted, m_dir.filePath(u"src/project"_s), {u"."_s});

        const auto names = [](const std::expected<std::vector<archives::Entry>, QString>& entries) {
            QStringList result;
            for (const archives::Entry& entry : entries.value())
                result << entry.path.section(u'/', -1) + (entry.isDir ? u"/"_s : QString());
            result.sort();
            return result;
        };
        QCOMPARE(names(archives::list(zip, {})), (QStringList {u"loose1.txt"_s, u"project/"_s}));
        QCOMPARE(names(archives::list(zip, u"project"_s)), (QStringList {u"code/"_s, u"readme.txt"_s}));
        QCOMPARE(names(archives::list(filesOnly, {})), (QStringList {u"loose2.txt"_s, u"project/"_s}));
        QCOMPARE(names(archives::list(filesOnly, u"project"_s)), QStringList {u"code/"_s});
        QCOMPARE(names(archives::list(filesOnly, u"project/code"_s)), QStringList {u"main.cpp"_s});
        QCOMPARE(names(archives::list(dotted, {})), (QStringList {u"code/"_s, u"readme.txt"_s}));

        QVERIFY(!archives::list(m_dir.filePath(u"src/loose1.txt"_s), {}));
    }

    void extractsSome()
    {
        const QString filesOnly = m_dir.filePath(u"files-only.tar"_s);
        const QString dotted = m_dir.filePath(u"dotted.tar.gz"_s);
        const QString out = m_dir.filePath(u"some"_s);

        // A file from a folder, without the folders above it.
        QVERIFY(archives::extractPaths(filesOnly, {u"project/code/main.cpp"_s}, out));
        QVERIFY(QFileInfo(out + u"/main.cpp"_s).isFile());
        // A folder that is only implied by what is in it, with its contents.
        QVERIFY(archives::extractPaths(filesOnly, {u"project/code"_s}, out + u"/2"_s));
        QVERIFY(QFileInfo(out + u"/2/code/main.cpp"_s).isFile());
        // Names stored with "./".
        QVERIFY(archives::extractPaths(dotted, {u"code"_s, u"readme.txt"_s}, out + u"/3"_s));
        QVERIFY(QFileInfo(out + u"/3/code/main.cpp"_s).isFile());
        QVERIFY(QFileInfo(out + u"/3/readme.txt"_s).isFile());

        QVERIFY(!archives::extractPaths(dotted, {u"missing.txt"_s}, out + u"/4"_s));
    }
};

QTEST_GUILESS_MAIN(TestArchives)
#include "tst_archives.moc"
