#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>

namespace ariadne {

// An application from a .desktop file.
struct DesktopApp {
    QString id; // e.g. "org.gnome.TextEditor.desktop"
    QString name;
    QString exec;
    QString iconName;
    QString filePath;
    QString workingDirectory;
    QStringList mimeTypes;
    bool terminal = false;
    bool noDisplay = false;
};

// What opening a file does when it can be run itself.
enum class Executable {
    No, // opened with an application
    Program, // a compiled program or AppImage: run
    Script, // text that can also be edited: ask
};

// Whether the file at `path`, of `mimeType`, runs when opened: it must be marked executable,
// and be a program or script. Libraries and documents that merely have the executable bit
// (as everything on FAT and NTFS drives does) are opened as usual.
Executable executableKind(const QString& path, const QString& mimeType);

// Runs a program or script in its own folder, in `terminal` (a program name or path, or empty
// for the default) when `inTerminal`.
std::expected<void, QString> runExecutable(const QString& path, bool inTerminal, const QString& terminal = {});

// Installed applications and which of them open which file types, following the
// freedesktop.org desktop entry and MIME applications specs, so defaults are shared with
// the rest of the desktop through mimeapps.list.
class Applications {
public:
    // Scans the application folders of the XDG data directories.
    static Applications load();

    // Applications meant to be shown in menus, sorted by name.
    QList<const DesktopApp*> visible() const;
    const DesktopApp* find(const QString& id) const;

    // Applications that open files of `mimeType`, best first: the default, then apps for
    // the exact type, then apps for its parent types (text/plain for source code, …).
    QList<const DesktopApp*> forMimeType(const QString& mimeType) const;
    const DesktopApp* defaultFor(const QString& mimeType) const;

    // Makes `appId` the default for `mimeType` in the user's mimeapps.list.
    static std::expected<void, QString> setDefault(const QString& mimeType, const QString& appId);

    // Starts `app` for `paths`, once for all of them or once per file, as its Exec line asks.
    // Terminal applications run in `preferredTerminal` when it is installed.
    static std::expected<void, QString> launch(
        const DesktopApp& app, const QStringList& paths, const QString& preferredTerminal = {});
    // The command lines launch() would run; for tests.
    static QList<QStringList> commandLines(const DesktopApp& app, const QStringList& paths);

private:
    void readMimeAppsLists();

    QList<DesktopApp> m_apps;
    QHash<QString, qsizetype> m_byId;
    QHash<QString, QStringList> m_defaults;
    QHash<QString, QStringList> m_added;
    QHash<QString, QStringList> m_removed;
};

} // namespace ariadne
