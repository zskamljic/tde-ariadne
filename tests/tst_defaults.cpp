#include "core/DefaultFileManager.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestDefaults : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    static QString read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private slots:
    void initTestCase()
    {
        qputenv("XDG_DATA_HOME", m_dir.filePath(u"home/data"_s).toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(u"home/config"_s).toLocal8Bit());
        qputenv("XDG_CONFIG_DIRS", m_dir.filePath(u"etc"_s).toLocal8Bit());
        qputenv("XDG_DATA_DIRS", m_dir.filePath(u"system"_s).toLocal8Bit());
        qputenv("XDG_CURRENT_DESKTOP", "TEST");
        // Nothing of the real session is touched.
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");

        // Ariadne must be installed to be a default.
        QVERIFY(QDir().mkpath(m_dir.filePath(u"system/applications"_s)));
        QFile desktop(m_dir.filePath(u"system/applications/ariadne.desktop"_s));
        QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write("[Desktop Entry]\nType=Application\nName=Ariadne\nExec=ariadne %U\nMimeType=inode/directory;\n");
    }

    void makeDefault()
    {
        QVERIFY(!defaults::status().opensFolders);
        QVERIFY(!defaults::status().complete());

        QVERIFY(defaults::makeDefault(u"/usr/bin/ariadne"_s));

        const defaults::Status status = defaults::status();
        QVERIFY(status.opensFolders);
        QVERIFY(status.showsFiles);
        QVERIFY(status.runsAtLogin);
        QCOMPARE(defaults::fileManagerServicePath(),
            m_dir.filePath(u"home/data/dbus-1/services/org.freedesktop.FileManager1.service"_s));
        QVERIFY(read(defaults::fileManagerServicePath()).contains(u"Exec=/usr/bin/ariadne --service\n"_s));
        QVERIFY(read(defaults::autostartPath()).contains(u"Exec=/usr/bin/ariadne --background\n"_s));
        QVERIFY(read(m_dir.filePath(u"home/config/mimeapps.list"_s)).contains(u"inode/directory=ariadne.desktop;"_s));

        // Doing it again changes nothing.
        QVERIFY(defaults::makeDefault(u"/usr/bin/ariadne"_s));
        QVERIFY(defaults::status().complete());
    }
};

QTEST_GUILESS_MAIN(TestDefaults)
#include "tst_defaults.moc"
