#include "core/Location.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestLocation : public QObject {
    Q_OBJECT

private slots:
    void parents()
    {
        QCOMPARE(location::parent(location::fromLocalPath(u"/usr/share"_s)), location::fromLocalPath(u"/usr"_s));
        QCOMPARE(location::parent(location::fromLocalPath(u"/usr"_s)), location::root());
        QVERIFY(!location::parent(location::root()));

        const QUrl trashChild = location::child(location::child(location::trash(), u"a"_s), u"b"_s);
        QCOMPARE(trashChild.toString(), u"trash:///a/b"_s);
        QCOMPARE(location::parent(trashChild), location::child(location::trash(), u"a"_s));
        QCOMPARE(location::parent(location::child(location::trash(), u"a"_s)), location::trash());
        QVERIFY(!location::parent(location::trash()));
    }

    void ancestors()
    {
        const QUrl usr = location::fromLocalPath(u"/usr"_s);
        QVERIFY(location::isAncestorOf(location::root(), usr));
        QVERIFY(location::isAncestorOf(usr, location::fromLocalPath(u"/usr/share/doc"_s)));
        QVERIFY(!location::isAncestorOf(usr, usr));
        QVERIFY(!location::isAncestorOf(usr, location::fromLocalPath(u"/usrlocal"_s)));
        QVERIFY(location::isAncestorOf(location::trash(), location::child(location::trash(), u"x"_s)));
        QVERIFY(!location::isAncestorOf(location::root(), location::trash()));
    }

    void userInput()
    {
        const QUrl current = location::fromLocalPath(u"/usr"_s);
        QCOMPARE(location::fromUserInput(u"~"_s, current), location::home());
        QCOMPARE(location::fromUserInput(u"~/Documents/"_s, current),
            location::fromLocalPath(QDir::homePath() + u"/Documents"_s));
        QCOMPARE(location::fromUserInput(u"/etc/../var"_s, current), location::fromLocalPath(u"/var"_s));
        QCOMPARE(location::fromUserInput(u"share"_s, current), location::fromLocalPath(u"/usr/share"_s));
        QCOMPARE(location::fromUserInput(u"file:///tmp/a%20b"_s, current), location::fromLocalPath(u"/tmp/a b"_s));
        QCOMPARE(location::fromUserInput(u"trash:///"_s, current), location::trash());
        QVERIFY(!location::fromUserInput(u"sftp://host/dir"_s, current));
        QVERIFY(!location::fromUserInput(u"   "_s, current));
    }

    void editableTextRoundTrips()
    {
        const QUrl current = location::root();
        for (const QUrl& url : {location::home(), location::fromLocalPath(QDir::homePath() + u"/x y"_s),
                 location::fromLocalPath(u"/etc"_s), location::trash(), location::child(location::trash(), u"old"_s)}) {
            QCOMPARE(location::fromUserInput(location::editableText(url), current), url);
        }
    }

    void crumbs()
    {
        const auto underHome = location::crumbs(location::fromLocalPath(QDir::homePath() + u"/a/b"_s));
        QCOMPARE(underHome.size(), 3u);
        QCOMPARE(underHome[0].label, u"Home"_s);
        QCOMPARE(underHome[2].label, u"b"_s);
        QCOMPARE(underHome[2].url, location::fromLocalPath(QDir::homePath() + u"/a/b"_s));

        const auto system = location::crumbs(location::fromLocalPath(u"/usr/share"_s));
        QCOMPARE(system.size(), 3u);
        QVERIFY(system[0].label.isEmpty());
        QCOMPARE(system[0].url, location::root());
        QCOMPARE(system[1].url, location::fromLocalPath(u"/usr"_s));

        QCOMPARE(location::crumbs(location::root()).size(), 1u);
        QCOMPARE(location::crumbs(location::child(location::trash(), u"x"_s)).size(), 2u);
    }

    void archives()
    {
        QTemporaryDir dir;
        const QString zip = dir.filePath(u"photos.zip"_s);
        QFile file(zip);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const QUrl top = location::archiveRoot(zip);
        QCOMPARE(top.toString(), u"archive://"_s + zip);
        QVERIFY(location::isArchive(top));
        QVERIFY(!location::isLocal(top));
        const QUrl inner = location::child(location::child(top, u"2024"_s), u"summer"_s);
        const auto place = location::archivePlace(inner);
        QVERIFY(place);
        QCOMPARE(place->file, zip);
        QCOMPARE(place->inside, u"2024/summer"_s);
        QCOMPARE(location::displayName(inner), u"summer"_s);
        QVERIFY(location::isAncestorOf(top, inner));

        // Up from the top of an archive is the folder it is in.
        QCOMPARE(location::parent(inner), location::child(top, u"2024"_s));
        QCOMPARE(location::parent(top), location::fromLocalPath(dir.path()));

        // Shown in the path bar with the archive as one step, and typed back in.
        const auto crumbs = location::crumbs(inner);
        QCOMPARE(crumbs.at(crumbs.size() - 3).label, u"photos.zip"_s);
        QCOMPARE(crumbs.at(crumbs.size() - 3).url, top);
        QCOMPARE(crumbs.back().url, inner);
        QCOMPARE(location::fromUserInput(location::editableText(inner), location::home()), inner);
        QCOMPARE(location::fromUserInput(location::editableText(top), location::home()), top);
        // The archive itself, without a slash, is still the file.
        QCOMPARE(location::fromUserInput(zip, location::home()), location::fromLocalPath(zip));
        QCOMPARE(location::fromUserInput(u"2024"_s, top), location::child(top, u"2024"_s));
    }
};

QTEST_GUILESS_MAIN(TestLocation)
#include "tst_location.moc"
