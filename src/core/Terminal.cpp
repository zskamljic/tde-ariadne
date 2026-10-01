#include "Terminal.hpp"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace ariadne::terminal {
namespace {

struct Known {
    QString name;
    QStringList commandPrefix;
    std::function<QStringList(const QString&)> directoryArguments;
};

QStringList none(const QString&)
{
    return {}; // starts in the folder it is started from
}

std::function<QStringList(const QString&)> option(const QString& name)
{
    return [name](const QString& directory) { return QStringList {name + directory}; };
}

std::function<QStringList(const QString&)> separate(const QString& name)
{
    return [name](const QString& directory) { return QStringList {name, directory}; };
}

// Terminals started through a server (GNOME's, for one) ignore the folder they are launched
// from, so each gets told the folder the way it understands.
const QList<Known>& knownTerminals()
{
    static const QList<Known> known {
        {u"xdg-terminal-exec"_s, {}, none},
        {u"kgx"_s, {u"--"_s}, option(u"--working-directory="_s)},
        // Ghostty's own working-directory setting (often "home") would win over the folder
        // it is started in; the command line wins over that.
        {u"ghostty"_s, {u"-e"_s}, option(u"--working-directory="_s)},
        {u"ptyxis"_s, {u"--"_s}, option(u"--working-directory="_s)},
        {u"gnome-terminal"_s, {u"--"_s}, option(u"--working-directory="_s)},
        {u"konsole"_s, {u"-e"_s}, separate(u"--workdir"_s)},
        {u"kitty"_s, {}, option(u"--directory="_s)},
        {u"alacritty"_s, {u"-e"_s}, separate(u"--working-directory"_s)},
        {u"foot"_s, {}, option(u"--working-directory="_s)},
        {u"wezterm"_s, {u"start"_s, u"--"_s},
            [](const QString& directory) { return QStringList {u"start"_s, u"--cwd"_s, directory}; }},
        {u"xfce4-terminal"_s, {u"-x"_s}, option(u"--working-directory="_s)},
        {u"xterm"_s, {u"-e"_s}, none},
    };
    return known;
}

std::optional<Terminal> resolve(const QString& program)
{
    const QString path = QStandardPaths::findExecutable(program);
    if (path.isEmpty())
        return std::nullopt;
    const QString name = QFileInfo(path).fileName();
    for (const Known& known : knownTerminals()) {
        if (known.name == name)
            return Terminal {path, known.commandPrefix, known.directoryArguments};
    }
    // Unknown terminals mostly understand -e and start where they are started.
    return Terminal {path, {u"-e"_s}, none};
}

} // namespace

QStringList Terminal::openIn(const QString& directory) const
{
    return QStringList {program} + directoryArguments(directory);
}

QStringList Terminal::run(const QStringList& command) const
{
    return QStringList {program} + commandPrefix + command;
}

std::optional<Terminal> find(const QString& preferred)
{
    for (const QString& candidate : {preferred, qEnvironmentVariable("TERMINAL")}) {
        if (candidate.isEmpty())
            continue;
        if (auto terminal = resolve(candidate))
            return terminal;
    }
    for (const Known& known : knownTerminals()) {
        if (auto terminal = resolve(known.name))
            return terminal;
    }
    return std::nullopt;
}

std::expected<void, QString> openIn(const QString& directory, const QString& preferred)
{
    const auto terminal = find(preferred);
    if (!terminal)
        return std::unexpected(u"No terminal was found. Set one with terminal = \"…\" in ~/.config/tde/config.lua."_s);
    QStringList command = terminal->openIn(directory);
    const QString program = command.takeFirst();
    // The working directory as well, for terminals that take it from there.
    if (!QProcess::startDetached(program, command, directory))
        return std::unexpected(u"%1 could not be started."_s.arg(QFileInfo(program).fileName()));
    return {};
}

} // namespace ariadne::terminal
