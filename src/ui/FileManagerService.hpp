#pragma once

#include <QObject>
#include <QStringList>

namespace ariadne {

class Application;

// org.freedesktop.FileManager1, which other applications call to show a folder or to point at
// a file ("Show in Folder"). A second `ariadne` started while one is running hands its
// locations over through it too, so there is a single instance.
class FileManagerService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    static constexpr auto ServiceName = "io.github.zskamljic.Ariadne";
    static constexpr auto FileManagerName = "org.freedesktop.FileManager1";
    static constexpr auto ObjectPath = "/org/freedesktop/FileManager1";

    explicit FileManagerService(Application& app, QObject* parent = nullptr);

    // Takes the bus names: Ariadne's own, and FileManager1 whenever no other file manager has it.
    // False when another Ariadne already runs.
    bool registerOnBus();
    // Gives the names up, so another Ariadne can take them.
    void unregisterFromBus();

    // Asks Nautilus to quit if it holds FileManager1, so a running Ariadne gets it.
    static void takeOverFromNautilus();
    static bool isRunning();

    // Asks an Ariadne that is already running to show `uris`. False when there is none, or it
    // did not answer.
    static bool forwardToRunning(const QStringList& folders, const QStringList& items);

public slots:
    // `startupId` is the activation token that lets the new window take focus.
    void ShowFolders(const QStringList& uris, const QString& startupId);
    void ShowItems(const QStringList& uris, const QString& startupId);
    void ShowItemProperties(const QStringList& uris, const QString& startupId);

private:
    Application& m_app;
};

} // namespace ariadne
