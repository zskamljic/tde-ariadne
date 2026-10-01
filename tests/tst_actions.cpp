#include "core/Config.hpp"
#include "core/CustomActions.hpp"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestActions : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QMimeDatabase m_mimes;

    static CustomAction action(QStringList types, CustomAction::Selection selection = CustomAction::Selection::Any)
    {
        CustomAction action;
        action.name = u"Test"_s;
        action.command = {u"true"_s};
        action.types = std::move(types);
        action.selection = selection;
        return action;
    }

private slots:
    void parsesConfig()
    {
        const QString path = m_dir.filePath(u"config.lua"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("return {\n"
                   "    actions = {\n"
                   "        { name = \"Open in Code\", command = 'code --new-window \"%F\"', types = { \"text/*\", "
                   "\"inode/directory\" } },\n"
                   "        { name = \"To PNG\", command = { \"magick\", \"%f\", \"%f.png\" }, types = \"image/*\", "
                   "selection = \"single\",\n"
                   "          icon = \"image-x-generic\", shortcut = \"Ctrl+Alt+P\" },\n"
                   "        { name = \"Here\", command = \"ghostty\", selection = \"none\", terminal = true },\n"
                   "        { name = \"Broken\" },\n"
                   "        { name = \"Odd\", command = \"x\", selection = \"sometimes\" },\n"
                   "    },\n"
                   "}\n");
        file.close();

        Config config;
        const auto warnings = readConfigFile(path, config);
        QVERIFY(warnings);
        QCOMPARE(config.actions.size(), 4);
        QCOMPARE(warnings->size(), 2); // the action without a command, the unknown selection

        const CustomAction& code = config.actions[0];
        QCOMPARE(code.command, (QStringList {u"code"_s, u"--new-window"_s, u"%F"_s}));
        QCOMPARE(code.types, (QStringList {u"text/*"_s, u"inode/directory"_s}));
        QCOMPARE(code.selection, CustomAction::Selection::Any);

        const CustomAction& png = config.actions[1];
        QCOMPARE(png.command, (QStringList {u"magick"_s, u"%f"_s, u"%f.png"_s}));
        QCOMPARE(png.types, QStringList {u"image/*"_s});
        QCOMPARE(png.selection, CustomAction::Selection::Single);
        QCOMPARE(png.shortcut, u"Ctrl+Alt+P"_s);
        QCOMPARE(png.icon, u"image-x-generic"_s);

        QCOMPARE(config.actions[2].selection, CustomAction::Selection::None);
        QVERIFY(config.actions[2].terminal);
    }

    void matches()
    {
        const ActionTarget png {u"/a.png"_s, u"image/png"_s};
        const ActionTarget cpp {u"/a.cpp"_s, u"text/x-c++src"_s};
        const ActionTarget folder {u"/a"_s, u"inode/directory"_s};
        using Selection = CustomAction::Selection;

        QVERIFY(appliesTo(action({u"image/*"_s}), {png}, m_mimes));
        QVERIFY(!appliesTo(action({u"image/*"_s}), {png, cpp}, m_mimes)); // all must match
        QVERIFY(appliesTo(action({u"text/plain"_s}), {cpp}, m_mimes)); // C++ source is text
        QVERIFY(appliesTo(action({u"inode/directory"_s}), {folder}, m_mimes));
        QVERIFY(appliesTo(action({}), {png, cpp, folder}, m_mimes));
        QVERIFY(!appliesTo(action({}), {}, m_mimes)); // nothing selected

        QVERIFY(appliesTo(action({}, Selection::Single), {png}, m_mimes));
        QVERIFY(!appliesTo(action({}, Selection::Single), {png, cpp}, m_mimes));
        QVERIFY(appliesTo(action({}, Selection::Multiple), {png, cpp}, m_mimes));
        QVERIFY(!appliesTo(action({}, Selection::Multiple), {png}, m_mimes));
        QVERIFY(appliesTo(action({}, Selection::None), {}, m_mimes));
        QVERIFY(!appliesTo(action({}, Selection::None), {png}, m_mimes));
    }

    void expands()
    {
        CustomAction act = action({});
        act.command = {u"tool"_s, u"%F"_s, u"--out=%d/out"_s, u"%f.bak"_s, u"100%%"_s, u"%x"_s};
        const std::vector<ActionTarget> targets {{u"/home/me/my file.txt"_s, {}}, {u"/home/me/b.txt"_s, {}}};
        QCOMPARE(expandCommand(act, targets, u"/home/me"_s),
            (QStringList {u"tool"_s, u"/home/me/my file.txt"_s, u"/home/me/b.txt"_s, u"--out=/home/me/out"_s,
                u"/home/me/my file.txt.bak"_s, u"100%"_s, u"%x"_s}));

        act.command = {u"open"_s, u"%U"_s};
        QCOMPARE(
            expandCommand(act, {{u"/tmp/a b"_s, {}}}, u"/tmp"_s), (QStringList {u"open"_s, u"file:///tmp/a%20b"_s}));

        // With nothing selected, %f is the folder shown.
        act.command = {u"open"_s, u"%f"_s};
        QCOMPARE(expandCommand(act, {}, u"/tmp"_s), (QStringList {u"open"_s, u"/tmp"_s}));
    }

    void runs()
    {
        CustomAction act = action({});
        act.command = {u"touch"_s, u"%f.done"_s};
        const QString target = m_dir.filePath(u"input"_s);
        QVERIFY(runAction(act, {{target, {}}}, m_dir.path()));
        QTRY_VERIFY(QFileInfo::exists(target + u".done"_s));

        act.command = {u"no-such-program-anywhere"_s};
        const auto failed = runAction(act, {{target, {}}}, m_dir.path());
        QVERIFY(!failed);
        QVERIFY(failed.error().contains(u"no-such-program-anywhere"_s));
    }
};

QTEST_GUILESS_MAIN(TestActions)
#include "tst_actions.moc"
