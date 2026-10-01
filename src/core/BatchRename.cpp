#include "BatchRename.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace ariadne::batchrename {
namespace {

// The extension of a file name, with its dot; folders have none.
QString extensionOf(const QString& path, const QString& name)
{
    if (QFileInfo(path).isDir())
        return {};
    QString suffix = QMimeDatabase().suffixForFileName(name);
    if (suffix.isEmpty())
        suffix = QFileInfo(name).suffix();
    if (suffix.isEmpty() || name.size() <= suffix.size() + 1)
        return {};
    return u'.' + suffix;
}

bool exists(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

} // namespace

std::expected<std::vector<Plan>, QString> plan(const QStringList& paths, const Options& options)
{
    QRegularExpression expression;
    if (options.regex) {
        expression.setPattern(options.find);
        if (!options.caseSensitive)
            expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid())
            return std::unexpected(expression.errorString());
    }

    std::vector<Plan> plans;
    for (const QString& path : paths) {
        Plan plan;
        plan.path = QDir::cleanPath(path);
        plan.oldName = QFileInfo(path).fileName();
        const QString extension = options.includeExtension ? QString() : extensionOf(path, plan.oldName);
        QString stem = plan.oldName.chopped(extension.size());
        if (options.find.isEmpty())
            ;
        else if (options.regex)
            stem.replace(expression, options.replace);
        else
            stem.replace(
                options.find, options.replace, options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        plan.newName = stem + extension;
        plans.push_back(std::move(plan));
    }

    // Names every item would have afterwards, per folder, to find clashes.
    QHash<QString, int> finalNames;
    QSet<QString> renamedAway;
    for (const Plan& plan : plans) {
        const QString directory = QFileInfo(plan.path).absolutePath();
        ++finalNames[directory + u'/' + plan.newName];
        if (plan.changes())
            renamedAway.insert(plan.path);
    }
    for (Plan& plan : plans) {
        if (!plan.changes())
            continue;
        const QString target = QFileInfo(plan.path).absolutePath() + u'/' + plan.newName;
        if (plan.newName.isEmpty() || plan.newName == u"." || plan.newName == u"..")
            plan.problem = u"The name would be empty."_s;
        else if (plan.newName.contains(u'/'))
            plan.problem = u"Names cannot contain “/”."_s;
        else if (finalNames.value(target) > 1)
            plan.problem = u"Several items would get this name."_s;
        else if (exists(target) && !renamedAway.contains(target)
            && plan.newName.compare(plan.oldName, Qt::CaseInsensitive) != 0)
            plan.problem = u"“%1” already exists."_s.arg(plan.newName);
    }
    return plans;
}

Outcome renameAll(const QList<std::pair<QString, QString>>& moves)
{
    Outcome outcome;
    struct Pending {
        QString original;
        QString current;
        QString target;
    };
    QList<Pending> pending;
    for (const auto& [from, to] : moves) {
        if (from != to)
            pending << Pending {from, from, to};
    }

    int temporary = 0;
    while (!pending.isEmpty()) {
        bool progress = false;
        for (qsizetype i = 0; i < pending.size();) {
            Pending& item = pending[i];
            const bool caseOnly = item.target.compare(item.current, Qt::CaseInsensitive) == 0;
            // A target held by another item that has yet to move away must wait for it.
            const bool heldByBatch = !caseOnly && std::ranges::any_of(pending, [&](const Pending& other) {
                return &other != &item && other.current == item.target;
            });
            if (heldByBatch) {
                ++i;
                continue;
            }
            if (!caseOnly && exists(item.target)) {
                outcome.failures << fileops::Failure {
                    item.original, u"“%1” already exists."_s.arg(QFileInfo(item.target).fileName())};
                pending.removeAt(i);
                progress = true;
                continue;
            }
            if (QDir().rename(item.current, item.target))
                outcome.renamed << std::pair {item.original, item.target};
            else
                outcome.failures << fileops::Failure {item.original, u"It could not be renamed."_s};
            pending.removeAt(i);
            progress = true;
        }
        if (progress || pending.isEmpty())
            continue;

        // Everything left waits on another: a cycle. Park one under a temporary name.
        Pending& item = pending.first();
        const QFileInfo info(item.current);
        QString parked;
        do {
            parked = info.absolutePath() + u"/.renaming-%1-"_s.arg(++temporary) + info.fileName();
        } while (exists(parked));
        if (QDir().rename(item.current, parked)) {
            item.current = parked;
        } else {
            outcome.failures << fileops::Failure {item.original, u"It could not be renamed."_s};
            pending.removeFirst();
        }
    }
    return outcome;
}

Outcome apply(const std::vector<Plan>& plans)
{
    QList<std::pair<QString, QString>> moves;
    for (const Plan& plan : plans) {
        if (plan.changes() && plan.problem.isEmpty())
            moves << std::pair {plan.path, QFileInfo(plan.path).absolutePath() + u'/' + plan.newName};
    }
    return renameAll(moves);
}

} // namespace ariadne::batchrename
