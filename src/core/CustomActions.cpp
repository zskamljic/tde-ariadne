#include "CustomActions.hpp"

#include "Terminal.hpp"

#include <QMimeDatabase>
#include <QProcess>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

QString uriOf(const QString& path)
{
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

bool matchesType(const QString& mimeType, const QStringList& types, const QMimeDatabase& mimes)
{
    if (types.isEmpty())
        return true;
    const QMimeType mime = mimes.mimeTypeForName(mimeType);
    return std::ranges::any_of(types, [&](const QString& type) {
        if (type == u"*" || type == u"*/*")
            return true;
        if (type.endsWith(u"/*"))
            return mimeType.startsWith(type.chopped(1));
        return mimeType == type || (mime.isValid() && mime.inherits(type));
    });
}

// Replaces the placeholders inside one argument.
QString expandArgument(const QString& argument, const std::vector<ActionTarget>& targets, const QString& folder)
{
    const QString first = targets.empty() ? folder : targets.front().path;
    const auto joined = [&](bool asUrls) {
        QStringList parts;
        for (const ActionTarget& target : targets)
            parts << (asUrls ? uriOf(target.path) : target.path);
        return parts.join(u' ');
    };
    QString result;
    for (qsizetype i = 0; i < argument.size(); ++i) {
        if (argument[i] != u'%' || i + 1 == argument.size()) {
            result += argument[i];
            continue;
        }
        switch (argument[++i].unicode()) {
        case 'f':
            result += first;
            break;
        case 'F':
            result += joined(false);
            break;
        case 'u':
            result += uriOf(first);
            break;
        case 'U':
            result += joined(true);
            break;
        case 'd':
            result += folder;
            break;
        case '%':
            result += u'%';
            break;
        default: // not a placeholder; kept as written
            result += u'%';
            result += argument[i];
        }
    }
    return result;
}

} // namespace

bool appliesTo(const CustomAction& action, const std::vector<ActionTarget>& targets, const QMimeDatabase& mimes)
{
    using Selection = CustomAction::Selection;
    switch (action.selection) {
    case Selection::None:
        return targets.empty();
    case Selection::Single:
        if (targets.size() != 1)
            return false;
        break;
    case Selection::Multiple:
        if (targets.size() < 2)
            return false;
        break;
    case Selection::Any:
        if (targets.empty())
            return false;
        break;
    }
    return std::ranges::all_of(
        targets, [&](const ActionTarget& target) { return matchesType(target.mimeType, action.types, mimes); });
}

QStringList expandCommand(const CustomAction& action, const std::vector<ActionTarget>& targets, const QString& folder)
{
    QStringList command;
    for (const QString& argument : action.command) {
        // Alone, %F and %U give every file an argument of its own, so names with spaces survive.
        if (argument == u"%F" || argument == u"%U") {
            for (const ActionTarget& target : targets)
                command << (argument == u"%U" ? uriOf(target.path) : target.path);
            continue;
        }
        command << expandArgument(argument, targets, folder);
    }
    return command;
}

std::expected<void, QString> runAction(const CustomAction& action, const std::vector<ActionTarget>& targets,
    const QString& folder, const QString& terminal)
{
    QStringList command = expandCommand(action, targets, folder);
    if (command.isEmpty())
        return std::unexpected(u"“%1” has no command to run."_s.arg(action.name));
    if (action.terminal) {
        const auto found = terminal::find(terminal);
        if (!found)
            return std::unexpected(u"“%1” needs a terminal, and none was found."_s.arg(action.name));
        command = found->run(command);
    }
    const QString program = command.takeFirst();
    if (!QProcess::startDetached(program, command, folder))
        return std::unexpected(u"“%1” could not be started: %2 was not found."_s.arg(action.name, program));
    return {};
}

} // namespace ariadne
