#include "core/FileOperations.hpp"
#include "core/Location.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestFileOperations : public QObject {
    Q_OBJECT

private:
    QString makeFile(const QString& relative)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            qFatal("cannot create %s", qPrintable(path));
        file.write("content");
        return path;
    }

    QString trashInfo(const QString& name) const
    {
        return QFileInfo(location::trashFilesPath()).absolutePath() + u"/info/"_s + name + u".trashinfo"_s;
    }

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        // Keep the trash inside the temporary folder, on the same file system as the files.
        qputenv("XDG_DATA_HOME", m_dir.filePath(u"data"_s).toLocal8Bit());
        QVERIFY(location::trashFilesPath().startsWith(m_dir.path()));
    }

    void trashesFilesAndFolders()
    {
        const QString file = makeFile(u"work/note.txt"_s);
        makeFile(u"work/folder/inner.txt"_s);
        const QString folder = m_dir.filePath(u"work/folder"_s);

        QVERIFY(fileops::moveToTrash({file, folder}).failures.isEmpty());
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!QFileInfo::exists(folder));
        QVERIFY(QFileInfo::exists(location::trashFilesPath() + u"/note.txt"_s));
        QVERIFY(QFileInfo::exists(location::trashFilesPath() + u"/folder/inner.txt"_s));

        QFile info(trashInfo(u"note.txt"_s));
        QVERIFY(info.open(QIODevice::ReadOnly));
        const QByteArray text = info.readAll();
        QVERIFY(text.startsWith("[Trash Info]"));
        QVERIFY(text.contains("Path=" + QFile::encodeName(file)));
    }

    void deletingFromTrashRemovesRestoreInformation()
    {
        const QString trashed = location::trashFilesPath() + u"/note.txt"_s;
        QVERIFY(QFileInfo::exists(trashInfo(u"note.txt"_s)));
        QVERIFY(fileops::deletePermanently({trashed}).isEmpty());
        QVERIFY(!QFileInfo::exists(trashed));
        QVERIFY(!QFileInfo::exists(trashInfo(u"note.txt"_s)));
    }

    void deletesFolderTrees()
    {
        makeFile(u"tree/a/b/c.txt"_s);
        makeFile(u"tree/d.txt"_s);
        const QString tree = m_dir.filePath(u"tree"_s);
        QVERIFY(fileops::deletePermanently({tree}).isEmpty());
        QVERIFY(!QFileInfo::exists(tree));
    }

    void deletingASymlinkKeepsItsTarget()
    {
        const QString target = makeFile(u"target/kept.txt"_s);
        const QString link = m_dir.filePath(u"link"_s);
        QVERIFY(QFile::link(QFileInfo(target).absolutePath(), link));
        QVERIFY(fileops::deletePermanently({link}).isEmpty());
        QVERIFY(!QFileInfo(link).isSymLink());
        QVERIFY(QFileInfo::exists(target));
    }

    void restoresToTheOriginalPlace()
    {
        const QString file = makeFile(u"docs/letter.txt"_s);
        QVERIFY(fileops::moveToTrash({file}).failures.isEmpty());
        QVERIFY(QDir(m_dir.filePath(u"docs"_s)).removeRecursively());
        const QString trashed = location::trashFilesPath() + u"/letter.txt"_s;
        QCOMPARE(fileops::originalPath(trashed), file);

        QVERIFY(fileops::restoreFromTrash({trashed}).isEmpty());
        QVERIFY(QFileInfo::exists(file)); // its folder was created again
        QVERIFY(!QFileInfo::exists(trashed));
        QVERIFY(!QFileInfo::exists(trashInfo(u"letter.txt"_s)));
    }

    void restoreRefusesToOverwrite()
    {
        const QString file = makeFile(u"clash.txt"_s);
        QVERIFY(fileops::moveToTrash({file}).failures.isEmpty());
        makeFile(u"clash.txt"_s);
        const auto failures = fileops::restoreFromTrash({location::trashFilesPath() + u"/clash.txt"_s});
        QCOMPARE(failures.size(), 1);
        QVERIFY(failures.first().reason.contains(u"already exists"_s));
        QVERIFY(QFileInfo::exists(location::trashFilesPath() + u"/clash.txt"_s));
    }

    void emptiesTheTrash()
    {
        makeFile(u"a/one.txt"_s);
        QVERIFY(fileops::moveToTrash({m_dir.filePath(u"a"_s), makeFile(u".hidden"_s)}).failures.isEmpty());
        QVERIFY(fileops::emptyTrash().isEmpty());
        QVERIFY(QDir(location::trashFilesPath()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
        QVERIFY(QDir(QFileInfo(location::trashFilesPath()).absolutePath() + u"/info"_s).isEmpty());
    }

    void uniqueNames()
    {
        makeFile(u"names/report.txt"_s);
        makeFile(u"names/report (2).txt"_s);
        makeFile(u"names/report (Copy).txt"_s);
        makeFile(u"names/backup.tar.gz"_s);
        makeFile(u"names/.bashrc"_s);
        QDir().mkpath(m_dir.filePath(u"names/Folder"_s));
        QDir().mkpath(m_dir.filePath(u"names/v1.2"_s));
        const QString directory = m_dir.filePath(u"names"_s);
        QCOMPARE(fileops::uniqueName(directory, u"free.txt"_s), u"free.txt"_s);
        QCOMPARE(fileops::uniqueName(directory, u"report.txt"_s), u"report (3).txt"_s);
        QCOMPARE(fileops::uniqueName(directory, u"report (2).txt"_s), u"report (3).txt"_s);
        QCOMPARE(fileops::uniqueName(directory, u"Folder"_s), u"Folder (2)"_s);
        QCOMPARE(fileops::uniqueName(directory, u"v1.2"_s), u"v1.2 (2)"_s); // a folder, no extension
        QCOMPARE(fileops::uniqueName(directory, u"backup.tar.gz"_s), u"backup (2).tar.gz"_s);

        QCOMPARE(fileops::duplicateName(directory, u"free.txt"_s), u"free (Copy).txt"_s);
        QCOMPARE(fileops::duplicateName(directory, u"report.txt"_s), u"report (Copy 2).txt"_s); // (Copy) is taken
        QCOMPARE(fileops::duplicateName(directory, u"report (Copy).txt"_s), u"report (Copy 2).txt"_s);
        QCOMPARE(fileops::duplicateName(directory, u"Folder"_s), u"Folder (Copy)"_s);
        QCOMPARE(fileops::duplicateName(directory, u".bashrc"_s), u".bashrc (Copy)"_s);
    }

    void undoesTrashing()
    {
        const QString file = makeFile(u"undo/me.txt"_s);
        const auto result = fileops::moveToTrash({file});
        QVERIFY(result.failures.isEmpty());
        QCOMPARE(result.trashed.size(), 1);
        QCOMPARE(result.trashed.first().original, file);
        QVERIFY(QFileInfo::exists(result.trashed.first().inTrash));

        QVERIFY(fileops::restoreTrashed(result.trashed).isEmpty());
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(!QFileInfo::exists(result.trashed.first().inTrash));
        QVERIFY(!QFileInfo::exists(trashInfo(u"me.txt"_s)));
    }

    void reportsFailures()
    {
        const QString missing = m_dir.filePath(u"missing.txt"_s);
        const auto trashFailures = fileops::moveToTrash({missing}).failures;
        QCOMPARE(trashFailures.size(), 1);
        QCOMPARE(trashFailures.first().path, missing);
        QVERIFY(!trashFailures.first().reason.isEmpty());
        QCOMPARE(fileops::deletePermanently({missing}).size(), 1);
    }

    void enclosedPermissions()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(u"root"_s);
        QVERIFY(QDir().mkpath(root + u"/inner/deeper"_s));
        for (const QString& name : {u"a.txt"_s, u"inner/b.txt"_s, u"inner/deeper/c.txt"_s}) {
            QFile file(root + u'/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(QFile::link(root + u"/a.txt"_s, root + u"/link"_s));
        const auto rw = QFile::ReadOwner | QFile::WriteOwner;
        const auto all = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;

        // Files: read-only for their owner. Folders: owner may do everything, nobody else anything.
        const auto failures = fileops::changeEnclosedPermissions(root,
            fileops::PermissionChange {
                rw | QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther, QFile::ReadOwner},
            fileops::PermissionChange {all | QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther
                    | QFile::WriteOther | QFile::ExeOther,
                all});
        QVERIFY(failures.isEmpty());

        const auto bits = [](const QString& path) {
            return QFileInfo(path).permissions() & ~(QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
        };
        for (const QString& name : {u"a.txt"_s, u"inner/b.txt"_s, u"inner/deeper/c.txt"_s})
            QCOMPARE(bits(root + u'/' + name), QFile::Permissions(QFile::ReadOwner));
        for (const QString& name : {u"inner"_s, u"inner/deeper"_s})
            QCOMPARE(bits(root + u'/' + name), QFile::Permissions(all));
        // The folder itself is left as it was.
        QVERIFY(bits(root) & QFile::ReadOther);

        // Taking access to folders away still reaches what is inside them.
        QVERIFY(
            fileops::changeEnclosedPermissions(root, fileops::PermissionChange {QFile::WriteOwner, QFile::WriteOwner},
                fileops::PermissionChange {QFile::ExeOwner, {}})
                .isEmpty());
        // Looking inside needs access to the folder again, one level at a time.
        QCOMPARE(bits(root + u"/inner"_s), QFile::Permissions(QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(QFile::setPermissions(root + u"/inner"_s, QFile::Permissions(all)));
        QCOMPARE(bits(root + u"/inner/deeper"_s), QFile::Permissions(QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(QFile::setPermissions(root + u"/inner/deeper"_s, QFile::Permissions(all)));
        QCOMPARE(bits(root + u"/inner/deeper/c.txt"_s), QFile::Permissions(QFile::ReadOwner | QFile::WriteOwner));
    }
};

QTEST_GUILESS_MAIN(TestFileOperations)
#include "tst_fileops.moc"
