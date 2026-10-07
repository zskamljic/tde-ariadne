#include "FileChooser.hpp"

#include <QDBusMetaType>
#include <QDir>
#include <QMimeDatabase>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace ariadne::chooser {
namespace {

// The answer to a choice, as the portal sends them back: its id and what was picked.
struct Picked {
    QString id;
    QString value;
};

QDBusArgument& operator<<(QDBusArgument& argument, const Picked& picked)
{
    argument.beginStructure();
    argument << picked.id << picked.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Picked& picked)
{
    argument.beginStructure();
    argument >> picked.id >> picked.value;
    argument.endStructure();
    return argument;
}

// An option that may come as it is, or still marshalled when it came over the bus.
template <typename T> T value(const QVariant& variant)
{
    if (variant.canConvert<QDBusArgument>())
        return qdbus_cast<T>(variant.value<QDBusArgument>());
    return variant.value<T>();
}

// Paths come as bytes, ended by a zero.
QString pathOf(const QVariant& variant)
{
    QByteArray bytes = value<QByteArray>(variant);
    if (bytes.endsWith('\0'))
        bytes.chop(1);
    return QString::fromLocal8Bit(bytes);
}

} // namespace

} // namespace ariadne::chooser

Q_DECLARE_METATYPE(ariadne::chooser::Picked)

namespace ariadne::chooser {

bool Filter::matches(const QString& fileName, const QString& mimeType) const
{
    static const QMimeDatabase mimes;
    const QMimeType type = mimes.mimeTypeForName(mimeType);
    for (const FilterRule& rule : rules) {
        if (rule.kind == FilterRule::MimeType) {
            if (rule.pattern == mimeType || (type.isValid() && type.inherits(rule.pattern)))
                return true;
            continue;
        }
        const QRegularExpression glob(
            QRegularExpression::wildcardToRegularExpression(rule.pattern), QRegularExpression::CaseInsensitiveOption);
        if (glob.match(fileName).hasMatch())
            return true;
    }
    return false;
}

Request Request::fromOptions(Mode mode, const QString& title, const QVariantMap& options)
{
    Request request;
    request.mode = mode;
    request.title = title;
    request.acceptLabel = options.value(u"accept_label"_s).toString();
    request.multiple = options.value(u"multiple"_s).toBool();
    request.directory = options.value(u"directory"_s).toBool();
    if (options.contains(u"filters"_s))
        request.filters = value<QList<Filter>>(options.value(u"filters"_s));
    if (options.contains(u"current_filter"_s))
        request.filter = int(request.filters.indexOf(value<Filter>(options.value(u"current_filter"_s))));
    if (options.contains(u"choices"_s))
        request.choices = value<QList<Choice>>(options.value(u"choices"_s));
    request.name = options.value(u"current_name"_s).toString();
    if (options.contains(u"current_folder"_s))
        request.folder = pathOf(options.value(u"current_folder"_s));
    // Saving over a file: its folder, and its name.
    if (options.contains(u"current_file"_s)) {
        const QString file = pathOf(options.value(u"current_file"_s));
        const int slash = int(file.lastIndexOf(u'/'));
        if (slash >= 0) {
            request.folder = file.left(std::max(slash, 1));
            if (request.name.isEmpty())
                request.name = file.mid(slash + 1);
        }
    }
    if (options.contains(u"files"_s)) {
        for (const QByteArray& file : value<QList<QByteArray>>(options.value(u"files"_s))) {
            QString name = QString::fromLocal8Bit(file.endsWith('\0') ? file.chopped(1) : file);
            request.files << name.mid(name.lastIndexOf(u'/') + 1);
        }
    }
    if (request.filter < 0 && !request.filters.isEmpty())
        request.filter = 0;
    return request;
}

QVariantMap results(const Request& request, const Answer& answer)
{
    QStringList uris;
    for (const QUrl& url : answer.urls)
        uris << url.toString(QUrl::FullyEncoded);
    QVariantMap results {{u"uris"_s, uris}};
    if (!answer.choices.isEmpty()) {
        QList<Picked> picked;
        for (const Choice& choice : answer.choices)
            picked.push_back({choice.id, choice.selected});
        results.insert(u"choices"_s, QVariant::fromValue(picked));
    }
    if (answer.filter >= 0 && answer.filter < request.filters.size())
        results.insert(u"current_filter"_s, QVariant::fromValue(request.filters[answer.filter]));
    // Files picked to open are only read.
    results.insert(u"writable"_s, request.mode != Mode::Open);
    return results;
}

void registerTypes()
{
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<QList<FilterRule>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<QList<Filter>>();
    qDBusRegisterMetaType<std::pair<QString, QString>>();
    qDBusRegisterMetaType<QList<std::pair<QString, QString>>>();
    qDBusRegisterMetaType<Choice>();
    qDBusRegisterMetaType<QList<Choice>>();
    qDBusRegisterMetaType<Picked>();
    qDBusRegisterMetaType<QList<Picked>>();
}

QDBusArgument& operator<<(QDBusArgument& argument, const FilterRule& rule)
{
    argument.beginStructure();
    argument << rule.kind << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, FilterRule& rule)
{
    argument.beginStructure();
    argument >> rule.kind >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const Filter& filter)
{
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Filter& filter)
{
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, const Choice& choice)
{
    argument.beginStructure();
    argument << choice.id << choice.label << choice.options << choice.selected;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, Choice& choice)
{
    argument.beginStructure();
    argument >> choice.id >> choice.label >> choice.options >> choice.selected;
    argument.endStructure();
    return argument;
}

} // namespace ariadne::chooser
