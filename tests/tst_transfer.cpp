#include "core/ClipboardFormat.hpp"
#include "core/Transfer.hpp"

#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestTransfer : public QObject {
    Q_OBJECT

private:
    QString makeFile(const QString& relative, const QByteArray& content = "content")
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qFatal("cannot create %s", qPrintable(path));
        file.write(content);
        return path;
    }

    QString makeDir(const QString& relative)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(path);
        return path;
    }

    static QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static TransferResult run(
        TransferKind kind, const QList<TransferItem>& items, std::function<ConflictAnswer(const Conflict&)> answer = {})
    {
        Transfer transfer(kind, items);
        if (answer)
            transfer.setConflictCallback(std::move(answer));
        return transfer.run();
    }

    QTemporaryDir m_dir;

private slots:
    void copiesFilesKeepingTimesAndPermissions()
    {
        const QString file = makeFile(u"copy/src/script.sh"_s, "#!/bin/sh\n");
        QFile::setPermissions(file, QFile::permissions(file) | QFile::ExeOwner);
        const QDateTime past = QDateTime::currentDateTime().addDays(-3);
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(past, QFileDevice::FileModificationTime));
        }
        const QString target = makeDir(u"copy/dst"_s);

        const auto result = run(TransferKind::Copy, Transfer::into({file}, target));
        QVERIFY(result.failures.isEmpty());
        const QString copy = target + u"/script.sh"_s;
        QCOMPARE(result.completed, (QList<TransferItem> {{file, copy}}));
        QCOMPARE(read(copy), "#!/bin/sh\n");
        QVERIFY(QFileInfo(copy).permissions() & QFile::ExeOwner);
        QCOMPARE(QFileInfo(copy).lastModified().toSecsSinceEpoch(), past.toSecsSinceEpoch());
        QVERIFY(QFileInfo::exists(file));
    }

    void copiesFolderTreesWithHiddenFilesAndLinks()
    {
        makeFile(u"tree/src/a/b/deep.txt"_s);
        makeFile(u"tree/src/.hidden"_s);
        QVERIFY(QFile::link(u"a/b/deep.txt"_s, m_dir.filePath(u"tree/src/link"_s)));
        const QString target = makeDir(u"tree/dst"_s);

        QList<TransferProgress> reports;
        Transfer transfer(TransferKind::Copy, Transfer::into({m_dir.filePath(u"tree/src"_s)}, target));
        transfer.setProgressCallback([&](const TransferProgress& progress) { reports << progress; });
        const auto result = transfer.run();
        QVERIFY(result.failures.isEmpty());
        QCOMPARE(read(target + u"/src/a/b/deep.txt"_s), "content");
        QVERIFY(QFileInfo::exists(target + u"/src/.hidden"_s));
        const QFileInfo link(target + u"/src/link"_s);
        QVERIFY(link.isSymLink());
        QCOMPARE(link.readSymLink(), u"a/b/deep.txt"_s); // still relative, not resolved

        QVERIFY(!reports.isEmpty());
        QCOMPARE(reports.last().totalFiles, 3);
        QCOMPARE(reports.last().doneFiles, 3);
        QCOMPARE(reports.last().doneBytes, reports.last().totalBytes);
    }

    void movesByRenaming()
    {
        const QString folder = makeDir(u"move/src/folder"_s);
        makeFile(u"move/src/folder/inside.txt"_s);
        const QString target = makeDir(u"move/dst"_s);

        const auto result = run(TransferKind::Move, Transfer::into({folder}, target));
        QVERIFY(result.failures.isEmpty());
        QVERIFY(!QFileInfo::exists(folder));
        QCOMPARE(read(target + u"/folder/inside.txt"_s), "content");
    }

    void movesAcrossFileSystems()
    {
        // The build folder and /tmp are usually on different file systems.
        QTemporaryDir other(QDir::currentPath() + u"/transfer-XXXXXX"_s);
        if (QStorageInfo(other.path()).rootPath() == QStorageInfo(m_dir.path()).rootPath())
            QSKIP("no second file system to move to");
        const QString file = makeFile(u"cross/file.txt"_s, "across");
        makeFile(u"cross/folder/inner.txt"_s);

        const auto result
            = run(TransferKind::Move, Transfer::into({file, m_dir.filePath(u"cross/folder"_s)}, other.path()));
        QVERIFY(result.failures.isEmpty());
        QCOMPARE(read(other.filePath(u"file.txt"_s)), "across");
        QCOMPARE(read(other.filePath(u"folder/inner.txt"_s)), "content");
        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!QFileInfo::exists(m_dir.filePath(u"cross/folder"_s)));
    }

    void copyingOntoItselfMakesADuplicate()
    {
        const QString file = makeFile(u"same/report.txt"_s);
        const auto copy = run(TransferKind::Copy, Transfer::into({file}, m_dir.filePath(u"same"_s)));
        QCOMPARE(copy.completed.first().destination, m_dir.filePath(u"same/report (Copy).txt"_s));
        QVERIFY(QFileInfo::exists(m_dir.filePath(u"same/report (Copy).txt"_s)));

        const auto move = run(TransferKind::Move, Transfer::into({file}, m_dir.filePath(u"same"_s)));
        QVERIFY(move.failures.isEmpty());
        QVERIFY(move.completed.isEmpty());
        QVERIFY(QFileInfo::exists(file));
    }

    void refusesToPutAFolderInsideItself()
    {
        const QString folder = makeDir(u"self/folder"_s);
        const QString inside = makeDir(u"self/folder/sub"_s);
        for (const auto kind : {TransferKind::Copy, TransferKind::Move}) {
            const auto result = run(kind, Transfer::into({folder}, inside));
            QCOMPARE(result.failures.size(), 1);
        }
        QVERIFY(QFileInfo::exists(inside));
        QVERIFY(!QFileInfo::exists(inside + u"/folder"_s));
    }

    void conflicts()
    {
        const QString source = makeFile(u"conflict/src/a.txt"_s, "new");
        const QString target = makeDir(u"conflict/dst"_s);
        const QString existing = makeFile(u"conflict/dst/a.txt"_s, "old");
        const auto answer = [](ConflictChoice choice) {
            return [choice](const Conflict&) { return ConflictAnswer {choice, false}; };
        };

        auto result = run(TransferKind::Copy, Transfer::into({source}, target), answer(ConflictChoice::Skip));
        QVERIFY(result.completed.isEmpty());
        QCOMPARE(read(existing), "old");

        result = run(TransferKind::Copy, Transfer::into({source}, target), answer(ConflictChoice::KeepBoth));
        QCOMPARE(result.completed.first().destination, target + u"/a (2).txt"_s);
        QCOMPARE(read(target + u"/a (2).txt"_s), "new");

        result = run(TransferKind::Copy, Transfer::into({source}, target), answer(ConflictChoice::Replace));
        QVERIFY(result.failures.isEmpty());
        QCOMPARE(read(existing), "new");

        result = run(TransferKind::Copy, Transfer::into({source}, target), answer(ConflictChoice::Cancel));
        QVERIFY(result.cancelled);
    }

    void mergesFolders()
    {
        makeFile(u"merge/src/docs/new.txt"_s, "new");
        makeFile(u"merge/src/docs/both.txt"_s, "from source");
        makeFile(u"merge/dst/docs/old.txt"_s, "old");
        makeFile(u"merge/dst/docs/both.txt"_s, "from target");

        QList<Conflict> asked;
        const auto result = run(TransferKind::Move,
            Transfer::into({m_dir.filePath(u"merge/src/docs"_s)}, m_dir.filePath(u"merge/dst"_s)),
            [&](const Conflict& conflict) {
                asked << conflict;
                return conflict.folders ? ConflictAnswer {ConflictChoice::Merge, false}
                                        : ConflictAnswer {ConflictChoice::Replace, true};
            });
        QVERIFY(result.failures.isEmpty());
        QCOMPARE(asked.size(), 2);
        QVERIFY(asked[0].folders);
        QVERIFY(!asked[1].folders);
        const QString docs = m_dir.filePath(u"merge/dst/docs"_s);
        QCOMPARE(read(docs + u"/old.txt"_s), "old");
        QCOMPARE(read(docs + u"/new.txt"_s), "new");
        QCOMPARE(read(docs + u"/both.txt"_s), "from source");
        QVERIFY(!QFileInfo::exists(m_dir.filePath(u"merge/src/docs"_s)));
    }

    void applyToAllIsRemembered()
    {
        const QString first = makeFile(u"all/src/1.txt"_s, "new");
        const QString second = makeFile(u"all/src/2.txt"_s, "new");
        makeFile(u"all/dst/1.txt"_s, "old");
        makeFile(u"all/dst/2.txt"_s, "old");
        int asked = 0;
        const auto result = run(
            TransferKind::Copy, Transfer::into({first, second}, m_dir.filePath(u"all/dst"_s)), [&](const Conflict&) {
                ++asked;
                return ConflictAnswer {ConflictChoice::Replace, true};
            });
        QVERIFY(result.failures.isEmpty());
        QCOMPARE(asked, 1);
        QCOMPARE(read(m_dir.filePath(u"all/dst/2.txt"_s)), "new");
    }

    void refusesToReplaceItsOwnParent()
    {
        const QString inner = makeFile(u"parent/box/box"_s, "inner");
        const auto result = run(TransferKind::Move, Transfer::into({inner}, m_dir.filePath(u"parent"_s)),
            [](const Conflict&) { return ConflictAnswer {ConflictChoice::Replace, false}; });
        QCOMPARE(result.failures.size(), 1);
        QCOMPARE(read(inner), "inner");
    }

    void cancellingRemovesPartialCopies()
    {
        const QString big = makeFile(u"cancel/big.bin"_s, QByteArray(8 * 1024 * 1024, 'x'));
        const QString target = makeDir(u"cancel/dst"_s);
        Transfer transfer(TransferKind::Copy, Transfer::into({big}, target));
        transfer.setProgressCallback(
            [&](const TransferProgress& progress) {
                if (progress.doneBytes > 0)
                    transfer.cancel();
            },
            0);
        const auto result = transfer.run();
        QVERIFY(result.cancelled);
        QVERIFY(!QFileInfo::exists(target + u"/big.bin"_s));
        QVERIFY(QFileInfo::exists(big));
    }

    void clipboardRoundTrips()
    {
        const clipboard::Files files {{u"/tmp/a b.txt"_s, u"/home/x/ž.png"_s}, true};
        const auto data = clipboard::encode(files);
        QCOMPARE(data->data(u"x-special/gnome-copied-files"_s),
            QByteArray("cut\nfile:///tmp/a%20b.txt\nfile:///home/x/%C5%BE.png"));
        QCOMPARE(clipboard::decode(data.get()), files);

        QMimeData kde;
        kde.setUrls({QUrl::fromLocalFile(u"/x/y"_s), QUrl(u"https://example.com"_s)});
        kde.setData(u"application/x-kde-cutselection"_s, "1");
        QCOMPARE(clipboard::decode(&kde), (clipboard::Files {{u"/x/y"_s}, true}));

        QMimeData text;
        text.setText(u"just text"_s);
        QVERIFY(!clipboard::decode(&text));
    }
};

QTEST_GUILESS_MAIN(TestTransfer)
#include "tst_transfer.moc"
