#include "core/Config.hpp"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

class TestConfig : public QObject {
    Q_OBJECT

private:
    QString writeFile(const QString& name, const QByteArray& source)
    {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qFatal("cannot write %s", qPrintable(path));
        file.write(source);
        return path;
    }

    static void setModified(const QString& path, const QDateTime& time)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime));
    }

    QTemporaryDir m_dir;

private slots:
    void applicationConfig()
    {
        Config config;
        const auto warnings
            = readConfigFile(writeFile(u"config.lua"_s,
                                 "return { icons = { [\"application/pdf\"] = \"x-office-document\" },\n"
                                 "         view = { mode = \"list\", sort = \"modified\", descending = true,\n"
                                 "                  grid_icon_size = 96, show_hidden = 1 } }\n"),
                config);
        QVERIFY(warnings.has_value());
        QCOMPARE(warnings->size(), 1);
        QCOMPARE(config.view.mode, ViewMode::List);
        QCOMPARE(config.view.sortKey, SortKey::Modified);
        QVERIFY(config.view.sortDescending);
        QCOMPARE(config.view.gridIconSize, 96);
        QVERIFY(!config.view.showHidden);
        QCOMPARE(config.icons.value(u"application/pdf"_s), u"x-office-document"_s);
    }

    void stateRoundTrips()
    {
        Config saved;
        saved.window = {1234, 567, true, 300};
        saved.view.mode = ViewMode::List;
        saved.view.sortKey = SortKey::Size;
        saved.view.sortDescending = true;
        saved.view.showHidden = true;
        saved.view.gridIconSize = 128;
        const QString state = m_dir.filePath(u"state.lua"_s);
        QVERIFY(saveState(saved, state));

        Config loaded;
        const auto warnings = readConfigFile(state, loaded);
        QVERIFY(warnings.has_value());
        QVERIFY2(warnings->isEmpty(), qPrintable(warnings->join(u'\n')));
        QVERIFY(loaded == saved);
    }

    void stateWinsUnlessConfigIsNewer()
    {
        const QString config
            = writeFile(u"layer-config.lua"_s, "return { view = { mode = \"list\", sort = \"type\" } }");
        Config remembered;
        remembered.view.mode = ViewMode::Grid;
        remembered.view.sortKey = SortKey::Size;
        const QString state = m_dir.filePath(u"layer-state.lua"_s);
        QVERIFY(saveState(remembered, state));

        const QDateTime now = QDateTime::currentDateTime();
        setModified(config, now.addSecs(-60));
        setModified(state, now);
        QCOMPARE(loadConfig(config, state).view.mode, ViewMode::Grid);

        setModified(config, now.addSecs(60));
        const Config edited = loadConfig(config, state);
        QCOMPARE(edited.view.mode, ViewMode::List);
        QCOMPARE(edited.view.sortKey, SortKey::Type);

        QCOMPARE(loadConfig(config, m_dir.filePath(u"missing.lua"_s)).view.mode, ViewMode::List);
    }

    void folderSettings()
    {
        const QString file = m_dir.filePath(u"folders.lua"_s);
        const QString kept = m_dir.filePath(u"kept"_s);
        const QString gone = m_dir.filePath(u"gone"_s);
        QDir().mkpath(kept);
        QDir().mkpath(gone);
        {
            FolderSettings settings(file);
            settings.set(kept, {ViewMode::List, SortKey::Modified, true});
            settings.set(gone, {ViewMode::List, SortKey::Size, false});
            settings.set(u"trash:///"_s, {ViewMode::List, SortKey::Name, false});
            QVERIFY(settings.save());
        }
        QDir().rmdir(gone);

        const FolderSettings loaded(file);
        QCOMPARE(loaded.find(kept), (FolderView {ViewMode::List, SortKey::Modified, true}));
        QVERIFY(!loaded.find(gone)); // forgotten, as it no longer exists
        QVERIFY(loaded.find(u"trash:///"_s));
        QVERIFY(!loaded.find(m_dir.filePath(u"never"_s)));
    }

    void errors()
    {
        Config config;
        QVERIFY(!readConfigFile(writeFile(u"bad.lua"_s, "return {"), config).has_value());
        QVERIFY(!readConfigFile(writeFile(u"bad.lua"_s, "error('boom')"), config).has_value());
        QVERIFY(!readConfigFile(writeFile(u"bad.lua"_s, "return 42"), config).has_value());
        QVERIFY(!readConfigFile(m_dir.filePath(u"missing.lua"_s), config).has_value());
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_config.moc"
