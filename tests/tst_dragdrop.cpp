#include "ui/DragDrop.hpp"

#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestDragDrop : public QObject {
    Q_OBJECT

private:
    QString makeFile(const QString& relative)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            qFatal("cannot create %s", qPrintable(path));
        return path;
    }

    QTemporaryDir m_dir;
    static constexpr Qt::DropActions All = Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;

private slots:
    void localPathsOnly()
    {
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(u"/a/b"_s), QUrl(u"https://example.com/x"_s)});
        QCOMPARE(dnd::localPaths(&data), QStringList {u"/a/b"_s});
        QVERIFY(dnd::localPaths(nullptr).isEmpty());
    }

    void movesWithinAFileSystemAndHonoursModifiers()
    {
        const QString file = makeFile(u"a/file.txt"_s);
        const QString target = m_dir.filePath(u"b"_s);
        QDir().mkpath(target);
        QCOMPARE(dnd::choose({file}, target, {}, All), Qt::MoveAction);
        QCOMPARE(dnd::choose({file}, target, Qt::ControlModifier, All), Qt::CopyAction);
        QCOMPARE(dnd::choose({file}, target, Qt::ShiftModifier, All), Qt::MoveAction);
        // A source that only allows copying gets a copy.
        QCOMPARE(dnd::choose({file}, target, {}, Qt::CopyAction), Qt::CopyAction);
    }

    void copiesAcrossFileSystems()
    {
        QTemporaryDir other(QDir::currentPath() + u"/dragdrop-XXXXXX"_s);
        if (QStorageInfo(other.path()).rootPath() == QStorageInfo(m_dir.path()).rootPath())
            QSKIP("no second file system");
        const QString file = makeFile(u"x/file.txt"_s);
        QCOMPARE(dnd::choose({file}, other.path(), {}, All), Qt::CopyAction);
        QCOMPARE(dnd::choose({file}, other.path(), Qt::ShiftModifier, All), Qt::MoveAction);
    }

    void refusesPointlessDrops()
    {
        const QString file = makeFile(u"same/file.txt"_s);
        const QString folder = m_dir.filePath(u"same"_s);
        QCOMPARE(dnd::choose({file}, folder, {}, All), Qt::IgnoreAction); // already there
        QCOMPARE(dnd::choose({file}, folder, Qt::ControlModifier, All), Qt::CopyAction); // but it can be duplicated
        QCOMPARE(dnd::choose({folder}, folder, {}, All), Qt::IgnoreAction); // onto itself
        QDir().mkpath(folder + u"/inner"_s);
        QCOMPARE(dnd::choose({folder}, folder + u"/inner"_s, {}, All), Qt::IgnoreAction); // into itself
        QCOMPARE(dnd::choose({file}, m_dir.filePath(u"missing"_s), {}, All), Qt::IgnoreAction);
    }
};

QTEST_GUILESS_MAIN(TestDragDrop)
#include "tst_dragdrop.moc"
