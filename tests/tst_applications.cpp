#include "core/Applications.hpp"
#include "core/Terminal.hpp"

#include <QDir>
#include <QFile>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestApplications : public QObject {
    Q_OBJECT

private:
    void write(const QString& relative, const QByteArray& content)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    static QStringList names(const QList<const DesktopApp*>& apps)
    {
        QStringList result;
        for (const DesktopApp* app : apps)
            result << app->id;
        return result;
    }

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        qputenv("XDG_DATA_HOME", m_dir.filePath(u"home/data"_s).toLocal8Bit());
        qputenv("XDG_DATA_DIRS", m_dir.filePath(u"system"_s).toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(u"home/config"_s).toLocal8Bit());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(u"etc"_s).toLocal8Bit());
        qputenv("XDG_CURRENT_DESKTOP", "TEST");
        // Qt finds the MIME database through XDG_DATA_DIRS too; keep the real one reachable.
        QDir().mkpath(m_dir.filePath(u"system"_s));
        QVERIFY(QFile::link(u"/usr/share/mime"_s, m_dir.filePath(u"system/mime"_s)));

        write(u"system/applications/editor.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Editor\nName[de]=Bearbeiter\n"
            "Exec=editor --new-window %F\nIcon=accessories-text-editor\nMimeType=text/plain;\n");
        write(u"system/applications/ide.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=IDE\nExec=ide %f\nMimeType=text/x-c++src;\n");
        write(u"system/applications/viewer.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Viewer\nExec=\"/opt/My Viewer/viewer\" --title=\"%c\" %U\n"
            "MimeType=image/png;\n");
        write(u"system/applications/hidden-helper.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Helper\nExec=helper %u\nNoDisplay=true\nMimeType=text/plain;\n");
        write(u"system/applications/tool.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Tool\nExec=tool %i 100%% %f\nIcon=tool\nTerminal=true\n");
        write(u"system/applications/gone.desktop"_s, "[Desktop Entry]\nType=Application\nName=Gone\nExec=gone\n");
        // The user's copy hides the system one.
        write(u"home/data/applications/gone.desktop"_s, "[Desktop Entry]\nType=Application\nName=Gone\nHidden=true\n");
        write(u"system/applications/org.kde/nested.desktop"_s,
            "[Desktop Entry]\nType=Application\nName=Nested\nExec=nested\n");
        write(u"etc/test-mimeapps.list"_s, "[Default Applications]\ntext/plain=missing.desktop;editor.desktop;\n");
    }

    void scansApplications()
    {
        const Applications apps = Applications::load();
        QVERIFY(apps.find(u"editor.desktop"_s));
        QVERIFY(apps.find(u"org.kde-nested.desktop"_s));
        QVERIFY(!apps.find(u"gone.desktop"_s));
        QCOMPARE(apps.find(u"editor.desktop"_s)->iconName, u"accessories-text-editor"_s);
        const QStringList visible = names(apps.visible());
        QVERIFY(!visible.contains(u"hidden-helper.desktop"_s));
        QCOMPARE(visible.first(), u"editor.desktop"_s); // sorted by name
    }

    void findsApplicationsForTypes()
    {
        const Applications apps = Applications::load();
        QCOMPARE(apps.defaultFor(u"text/plain"_s)->id, u"editor.desktop"_s);
        // C++ sources: the specific IDE first after the inherited default, then other text apps.
        QCOMPARE(names(apps.forMimeType(u"text/x-c++src"_s)),
            (QStringList {u"editor.desktop"_s, u"ide.desktop"_s, u"hidden-helper.desktop"_s}));
        QVERIFY(apps.forMimeType(u"application/pdf"_s).isEmpty());
    }

    void setsDefaults()
    {
        write(u"home/config/mimeapps.list"_s, "[Added Associations]\nimage/png=other.desktop;\n\n[Custom]\nkeep=me\n");
        QVERIFY(Applications::setDefault(u"text/plain"_s, u"ide.desktop"_s).has_value());
        QVERIFY(Applications::setDefault(u"image/png"_s, u"viewer.desktop"_s).has_value());

        const Applications apps = Applications::load();
        QCOMPARE(apps.defaultFor(u"text/plain"_s)->id, u"ide.desktop"_s);
        QCOMPARE(apps.defaultFor(u"image/png"_s)->id, u"viewer.desktop"_s);

        QFile file(m_dir.filePath(u"home/config/mimeapps.list"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.contains("image/png=viewer.desktop;other.desktop;"));
        QVERIFY(text.contains("[Custom]\nkeep=me"));
    }

    void buildsCommandLines()
    {
        const Applications apps = Applications::load();
        const QStringList files {u"/tmp/a b.txt"_s, u"/tmp/c.txt"_s};

        QCOMPARE(Applications::commandLines(*apps.find(u"editor.desktop"_s), files),
            (QList<QStringList> {{u"editor"_s, u"--new-window"_s, u"/tmp/a b.txt"_s, u"/tmp/c.txt"_s}}));
        QCOMPARE(Applications::commandLines(*apps.find(u"ide.desktop"_s), files),
            (QList<QStringList> {{u"ide"_s, u"/tmp/a b.txt"_s}, {u"ide"_s, u"/tmp/c.txt"_s}}));
        QCOMPARE(Applications::commandLines(*apps.find(u"viewer.desktop"_s), {u"/tmp/a b.png"_s}),
            (QList<QStringList> {{u"/opt/My Viewer/viewer"_s, u"--title=Viewer"_s, u"file:///tmp/a%20b.png"_s}}));
        QCOMPARE(Applications::commandLines(*apps.find(u"tool.desktop"_s), {u"/x"_s}),
            (QList<QStringList> {{u"tool"_s, u"--icon"_s, u"tool"_s, u"100%"_s, u"/x"_s}}));
        QCOMPARE(Applications::commandLines(*apps.find(u"ide.desktop"_s), {}), (QList<QStringList> {{u"ide"_s}}));
    }

    void findsTerminals()
    {
        // Fake terminals on a PATH of their own.
        const QString bin = m_dir.filePath(u"bin"_s);
        QDir().mkpath(bin);
        for (const QString& name : {u"foot"_s, u"myterm"_s}) {
            QFile file(bin + u'/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#!/bin/sh\n");
            file.setPermissions(file.permissions() | QFile::ExeOwner);
        }
        const QByteArray path = qgetenv("PATH");
        const QByteArray terminalVariable = qgetenv("TERMINAL");
        qputenv("PATH", bin.toLocal8Bit());
        qunsetenv("TERMINAL");

        auto terminal = terminal::find();
        QVERIFY(terminal);
        QCOMPARE(terminal->openIn(u"/x y"_s), (QStringList {bin + u"/foot"_s, u"--working-directory=/x y"_s}));
        QCOMPARE(terminal->run({u"htop"_s}), (QStringList {bin + u"/foot"_s, u"htop"_s}));

        terminal = terminal::find(u"myterm"_s);
        QCOMPARE(terminal->openIn(u"/x"_s), QStringList {bin + u"/myterm"_s});
        QCOMPARE(terminal->run({u"htop"_s}), (QStringList {bin + u"/myterm"_s, u"-e"_s, u"htop"_s}));

        {
            QFile file(bin + u"/ghostty"_s);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.setPermissions(file.permissions() | QFile::ExeOwner);
        }
        terminal = terminal::find(u"ghostty"_s);
        QCOMPARE(terminal->openIn(u"/x"_s), (QStringList {bin + u"/ghostty"_s, u"--working-directory=/x"_s}));
        QCOMPARE(terminal->run({u"htop"_s}), (QStringList {bin + u"/ghostty"_s, u"-e"_s, u"htop"_s}));

        qputenv("TERMINAL", "myterm");
        QCOMPARE(terminal::find(u"not-installed"_s)->program, bin + u"/myterm"_s);

        qputenv("PATH", path);
        if (terminalVariable.isNull())
            qunsetenv("TERMINAL");
        else
            qputenv("TERMINAL", terminalVariable);
    }

    void executables()
    {
        const QMimeDatabase mimeDatabase;
        const auto make = [&](const QString& name, const QByteArray& content, bool executable) {
            const QString path = m_dir.filePath(u"run/"_s + name);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            file.open(QIODevice::WriteOnly);
            file.write(content);
            file.close();
            if (executable)
                file.setPermissions(file.permissions() | QFile::ExeOwner);
            return path;
        };
        const auto kind
            = [&](const QString& path) { return executableKind(path, mimeDatabase.mimeTypeForFile(path).name()); };
        const auto copy = [&](const QString& from, const QString& name) {
            const QString path = m_dir.filePath(u"run/"_s + name);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile::copy(from, path);
            return path;
        };

        const QString program = copy(u"/usr/bin/true"_s, u"program"_s);
        QCOMPARE(kind(program), Executable::Program);
        const QString script = make(u"hello.sh"_s, "#!/bin/sh\ntouch \"$0.ran\"\n", true);
        QCOMPARE(kind(script), Executable::Script);
        QCOMPARE(kind(make(u"tool.py"_s, "#!/usr/bin/env python3\n", true)), Executable::Script);
        // Only what is marked executable runs.
        QCOMPARE(kind(make(u"idle.sh"_s, "#!/bin/sh\n", false)), Executable::No);
        // Executable bits on documents and libraries, as on FAT and NTFS drives, do not count.
        QCOMPARE(kind(make(u"notes.txt"_s, "hello\n", true)), Executable::No);
        QCOMPARE(kind(make(u"paper.pdf"_s, "%PDF-1.7\n", true)), Executable::No);
        const QString library = copy(u"/usr/lib/libz.so.1"_s, u"libz.so.1"_s);
        QCOMPARE(kind(library), Executable::No);

        // Runs in its own folder.
        QVERIFY(runExecutable(script, false));
        QTRY_VERIFY(QFileInfo::exists(script + u".ran"_s));
    }
};

QTEST_GUILESS_MAIN(TestApplications)
#include "tst_applications.moc"
