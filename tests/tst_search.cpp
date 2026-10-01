#include "core/BatchRename.hpp"
#include "core/Location.hpp"
#include "core/Search.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestSearchAndRename : public QObject {
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

    QStringList search(const QString& relative, const QString& query, bool hidden = false)
    {
        QStringList found;
        std::stop_source stop;
        search::run(location::fromLocalPath(m_dir.filePath(relative)), {query, hidden, 5000}, stop.get_token(),
            [&](std::vector<FileEntry> batch) {
                for (const FileEntry& entry : batch)
                    found << QDir(m_dir.path()).relativeFilePath(entry.path);
            });
        return found;
    }

    static QStringList names(const QString& directory)
    {
        return QDir(directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    }

    QTemporaryDir m_dir;

private slots:
    void matching()
    {
        QVERIFY(search::matches(u"Résumé 2024.pdf"_s, u"resume"_s));
        QVERIFY(search::matches(u"Holiday Photos Italy"_s, u"photos ital"_s));
        QVERIFY(!search::matches(u"Holiday Photos"_s, u"photos spain"_s));
        QVERIFY(search::matches(u"ÇA VA"_s, u"ça va"_s));
        QVERIFY(!search::matches(u"anything"_s, u"   "_s));
    }

    void searchesNearestFirst()
    {
        makeFile(u"s/deep/deeper/report-3.txt"_s);
        makeFile(u"s/deep/report-2.txt"_s);
        makeFile(u"s/report-1.txt"_s);
        makeFile(u"s/other.txt"_s);
        makeFile(u"s/.hidden/report-hidden.txt"_s);
        makeFile(u"s/.report-dot.txt"_s);
        QVERIFY(QFile::link(m_dir.filePath(u"s"_s), m_dir.filePath(u"s/deep/loop"_s)));

        QCOMPARE(search(u"s"_s, u"report"_s),
            (QStringList {u"s/report-1.txt"_s, u"s/deep/report-2.txt"_s, u"s/deep/deeper/report-3.txt"_s}));
        const QStringList withHidden = search(u"s"_s, u"report"_s, true);
        QCOMPARE(withHidden.size(), 5);
        QVERIFY(withHidden.contains(u"s/.hidden/report-hidden.txt"_s));
    }

    void stopsWhenAsked()
    {
        for (int i = 0; i < 50; ++i)
            makeFile(u"many/sub%1/match.txt"_s.arg(i));
        std::stop_source stop;
        int batches = 0;
        search::run(location::fromLocalPath(m_dir.filePath(u"many"_s)), {u"match"_s, false, 5000}, stop.get_token(),
            [&](std::vector<FileEntry>) {
                ++batches;
                stop.request_stop();
            });
        QVERIFY(batches <= 1);
    }

    void plansReplacements()
    {
        const QString a = makeFile(u"r/IMG_001.jpeg"_s);
        const QString b = makeFile(u"r/IMG_002.JPEG"_s);
        const QString c = makeFile(u"r/notes.txt"_s);

        auto plans = batchrename::plan({a, b, c}, {u"IMG_"_s, u"Holiday "_s});
        QVERIFY(plans.has_value());
        QCOMPARE(plans->at(0).newName, u"Holiday 001.jpeg"_s);
        QCOMPARE(plans->at(1).newName, u"Holiday 002.JPEG"_s);
        QVERIFY(!plans->at(2).changes());

        // Extensions stay out of it unless asked.
        plans = batchrename::plan({a, b}, {u"jpeg"_s, u"jpg"_s});
        QVERIFY(!plans->at(0).changes());
        plans = batchrename::plan({a, b}, {u"jpeg"_s, u"jpg"_s, false, false, true});
        QCOMPARE(plans->at(0).newName, u"IMG_001.jpg"_s);
        QCOMPARE(plans->at(1).newName, u"IMG_002.jpg"_s);
        plans = batchrename::plan({a, b}, {u"jpeg"_s, u"jpg"_s, false, true, true});
        QVERIFY(!plans->at(1).changes()); // JPEG does not match case-sensitively

        plans = batchrename::plan({a, b}, {u"IMG_(\\d+)"_s, u"photo-\\1-x"_s, true});
        QCOMPARE(plans->at(0).newName, u"photo-001-x.jpeg"_s);

        QVERIFY(!batchrename::plan({a}, {u"(unclosed"_s, u""_s, true}).has_value());
    }

    void findsProblems()
    {
        const QString a = makeFile(u"p/a1.txt"_s);
        const QString b = makeFile(u"p/b1.txt"_s);
        makeFile(u"p/taken.txt"_s);
        auto plans = batchrename::plan({a, b}, {u"[ab]1"_s, u"same"_s, true});
        QVERIFY(!plans->at(0).problem.isEmpty()); // both would be same.txt
        plans = batchrename::plan({a}, {u"a1"_s, u"taken"_s});
        QVERIFY(plans->at(0).problem.contains(u"already exists"_s));
        plans = batchrename::plan({a}, {u"a1"_s, u""_s});
        QVERIFY(plans->at(0).problem.isEmpty()); // ".txt" is a name, a hidden one
        plans = batchrename::plan({a}, {u"a1"_s, u"x/y"_s});
        QVERIFY(!plans->at(0).problem.isEmpty());
    }

    void renamesSwapsAndChains()
    {
        const QString a = makeFile(u"swap/a.txt"_s);
        const QString b = makeFile(u"swap/b.txt"_s);
        const QString c = makeFile(u"swap/c.txt"_s);
        // a → b, b → c, c → a: a full cycle.
        const auto outcome = batchrename::renameAll({{a, b}, {b, c}, {c, a}});
        QVERIFY(outcome.failures.isEmpty());
        QCOMPARE(outcome.renamed.size(), 3);
        QCOMPARE(names(m_dir.filePath(u"swap"_s)), (QStringList {u"a.txt"_s, u"b.txt"_s, u"c.txt"_s}));
    }

    void renamesAndUndoes()
    {
        const QString one = makeFile(u"u/one draft.txt"_s);
        const QString two = makeFile(u"u/two draft.txt"_s);
        auto plans = batchrename::plan({one, two}, {u" draft"_s, u""_s});
        const auto outcome = batchrename::apply(*plans);
        QVERIFY(outcome.failures.isEmpty());
        QCOMPARE(names(m_dir.filePath(u"u"_s)), (QStringList {u"one.txt"_s, u"two.txt"_s}));

        QList<std::pair<QString, QString>> back;
        for (const auto& [from, to] : outcome.renamed)
            back << std::pair {to, from};
        QVERIFY(batchrename::renameAll(back).failures.isEmpty());
        QCOMPARE(names(m_dir.filePath(u"u"_s)), (QStringList {u"one draft.txt"_s, u"two draft.txt"_s}));
    }

    void refusesToOverwrite()
    {
        const QString a = makeFile(u"o/a.txt"_s);
        makeFile(u"o/b.txt"_s);
        const auto outcome = batchrename::renameAll({{a, m_dir.filePath(u"o/b.txt"_s)}});
        QCOMPARE(outcome.failures.size(), 1);
        QVERIFY(QFileInfo::exists(a));
    }

    void changesOnlyCase()
    {
        const QString a = makeFile(u"case/readme.md"_s);
        const auto plans = batchrename::plan({a}, {u"readme"_s, u"README"_s});
        QVERIFY(plans->at(0).problem.isEmpty());
        QVERIFY(batchrename::apply(*plans).failures.isEmpty());
        QCOMPARE(names(m_dir.filePath(u"case"_s)), QStringList {u"README.md"_s});
    }
};

QTEST_GUILESS_MAIN(TestSearchAndRename)
#include "tst_search.moc"
