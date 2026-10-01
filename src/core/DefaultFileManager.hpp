#pragma once

#include <QString>

#include <expected>

// Making Ariadne the file manager of the user's desktop. All of it is per user, in the home
// directory, which no package may touch; so Ariadne offers to do it itself.
namespace ariadne::defaults {

struct Status {
    bool opensFolders = false; // the default for inode/directory in mimeapps.list
    bool showsFiles = false; // D-Bus starts Ariadne for org.freedesktop.FileManager1
    bool runsAtLogin = false; // kept running, so it holds FileManager1 before Nautilus can

    bool complete() const { return opensFolders && showsFiles && runsAtLogin; }
};

Status status();

// Sets up all three, starting `executable` (the Ariadne to use) from D-Bus and at login.
std::expected<void, QString> makeDefault(const QString& executable);

// Where the pieces go; exposed for tests.
QString fileManagerServicePath();
QString autostartPath();

} // namespace ariadne::defaults
