#pragma once

#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <optional>

namespace ariadne::terminal {

struct Terminal {
    QString program;
    QStringList commandPrefix; // what goes before a command to run in it, like "-e"
    std::function<QStringList(const QString&)> directoryArguments; // how to start in a folder

    // The command line that opens the terminal in `directory`.
    QStringList openIn(const QString& directory) const;
    // The command line that runs `command` in the terminal.
    QStringList run(const QStringList& command) const;
};

// The terminal to use: `preferred` (a program name or path, from the config) if it is
// installed, else $TERMINAL, else the first installed of the well-known ones.
std::optional<Terminal> find(const QString& preferred = {});

std::expected<void, QString> openIn(const QString& directory, const QString& preferred = {});

} // namespace ariadne::terminal
