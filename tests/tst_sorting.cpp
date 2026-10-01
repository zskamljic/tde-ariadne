#include "core/Location.hpp"
#include "ui/DirectoryModel.hpp"
#include "ui/FileSortProxy.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestSorting : public QObject {
    Q_OBJECT

private:
    QStringList sortedNames(bool caseSensitive, bool descending = false)
    {
        m_proxy.setCaseSensitive(caseSensitive);
        m_proxy.setSort(SortKey::Name, descending);
        QStringList names;
        for (int row = 0; row < m_proxy.rowCount(); ++row)
            names << m_proxy.index(row, 0).data().toString();
        return names;
    }

    QTemporaryDir m_dir;
    DirectoryModel m_model;
    FileSortProxy m_proxy;

private slots:
    void initTestCase()
    {
        for (const QString& name :
            {u"beta"_s, u"Alpha"_s, u"alpha2"_s, u"alpha10"_s, u"Zed"_s, u"apple"_s, u".hidden"_s}) {
            QFile file(m_dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(QDir(m_dir.path()).mkdir(u"folder"_s));

        m_proxy.setDirectoryModel(&m_model);
        QSignalSpy loaded(&m_model, &DirectoryModel::loaded);
        m_model.setLocation(location::fromLocalPath(m_dir.path()));
        QVERIFY(loaded.wait());
    }

    void caseInsensitiveByDefault()
    {
        QCOMPARE(sortedNames(false),
            (QStringList {u"folder"_s, u"Alpha"_s, u"alpha2"_s, u"alpha10"_s, u"apple"_s, u"beta"_s, u"Zed"_s}));
    }

    void caseSensitivePutsUppercaseFirst()
    {
        QCOMPARE(sortedNames(true),
            (QStringList {u"folder"_s, u"Alpha"_s, u"Zed"_s, u"alpha2"_s, u"alpha10"_s, u"apple"_s, u"beta"_s}));
    }

    void descendingKeepsFoldersFirst()
    {
        QCOMPARE(sortedNames(false, true),
            (QStringList {u"folder"_s, u"Zed"_s, u"beta"_s, u"apple"_s, u"alpha10"_s, u"alpha2"_s, u"Alpha"_s}));
    }

    void mixedFoldersAndFiles()
    {
        m_proxy.setFoldersFirst(false);
        const QStringList names = sortedNames(false);
        m_proxy.setFoldersFirst(true);
        QCOMPARE(
            names, (QStringList {u"Alpha"_s, u"alpha2"_s, u"alpha10"_s, u"apple"_s, u"beta"_s, u"folder"_s, u"Zed"_s}));
    }

    void hiddenFiles()
    {
        m_proxy.setShowHidden(true);
        QVERIFY(sortedNames(false).contains(u".hidden"_s));
        m_proxy.setShowHidden(false);
        QVERIFY(!sortedNames(false).contains(u".hidden"_s));
    }
};

QTEST_MAIN(TestSorting)
#include "tst_sorting.moc"
