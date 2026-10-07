#include "core/FileChooser.hpp"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne::chooser;

class TestFileChooser : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerTypes(); }

    void filtersMatchByGlobAndMimeType()
    {
        const Filter images {u"Images"_s, {{FilterRule::MimeType, u"image/*"_s}, {FilterRule::Glob, u"*.xcf"_s}}};
        const Filter pngs {u"PNG"_s, {{FilterRule::MimeType, u"image/png"_s}}};
        const Filter text {u"Text"_s, {{FilterRule::MimeType, u"text/plain"_s}}};
        QVERIFY(images.matches(u"drawing.XCF"_s, u"application/octet-stream"_s));
        QVERIFY(pngs.matches(u"a.png"_s, u"image/png"_s));
        QVERIFY(!pngs.matches(u"a.jpg"_s, u"image/jpeg"_s));
        // A kind of text is text.
        QVERIFY(text.matches(u"main.cpp"_s, u"text/x-c++src"_s));
        QVERIFY(!text.matches(u"a.png"_s, u"image/png"_s));
    }

    void optionsAreRead()
    {
        const Filter pngs {u"PNG"_s, {{FilterRule::MimeType, u"image/png"_s}}};
        const Filter all {u"All Files"_s, {{FilterRule::Glob, u"*"_s}}};
        const QVariantMap options {
            {u"accept_label"_s, u"_Export"_s},
            {u"multiple"_s, true},
            {u"filters"_s, QVariant::fromValue(QList<Filter> {pngs, all})},
            {u"current_filter"_s, QVariant::fromValue(all)},
            {u"current_file"_s, QByteArray("/home/me/Pictures/sea.png\0", 26)},
        };
        const Request request = Request::fromOptions(Mode::Save, u"Export"_s, options);
        QCOMPARE(request.acceptLabel, u"_Export"_s);
        QVERIFY(request.multiple);
        QCOMPARE(request.filters.size(), 2);
        QCOMPARE(request.filter, 1);
        QCOMPARE(request.folder, u"/home/me/Pictures"_s);
        QCOMPARE(request.name, u"sea.png"_s);
    }

    void firstFilterIsPickedWhenNoneIs()
    {
        const Filter pngs {u"PNG"_s, {{FilterRule::MimeType, u"image/png"_s}}};
        const Request request
            = Request::fromOptions(Mode::Open, {}, {{u"filters"_s, QVariant::fromValue(QList<Filter> {pngs})}});
        QCOMPARE(request.filter, 0);
        QCOMPARE(Request::fromOptions(Mode::Open, {}, {}).filter, -1);
    }

    void savingSeveralKeepsTheirNames()
    {
        const QList<QByteArray> files {QByteArray("/tmp/a.txt\0", 11), QByteArray("b.txt\0", 6)};
        const Request request = Request::fromOptions(Mode::SaveFiles, {}, {{u"files"_s, QVariant::fromValue(files)}});
        QCOMPARE(request.files, (QStringList {u"a.txt"_s, u"b.txt"_s}));
    }

    void resultsSayWhatWasPicked()
    {
        Request request;
        request.mode = Mode::Save;
        request.filters = {Filter {u"PNG"_s, {{FilterRule::MimeType, u"image/png"_s}}}};
        Answer answer;
        answer.urls = {QUrl::fromLocalFile(u"/home/me/a b.png"_s)};
        answer.filter = 0;
        answer.choices = {Choice {u"encoding"_s, u"Encoding"_s, {{u"utf8"_s, u"UTF-8"_s}}, u"utf8"_s}};
        const QVariantMap map = results(request, answer);
        QCOMPARE(map.value(u"uris"_s).toStringList(), QStringList {u"file:///home/me/a%20b.png"_s});
        QVERIFY(map.value(u"writable"_s).toBool());
        QVERIFY(map.contains(u"current_filter"_s));
        QVERIFY(map.contains(u"choices"_s));
    }
};

QTEST_GUILESS_MAIN(TestFileChooser)
#include "tst_filechooser.moc"
