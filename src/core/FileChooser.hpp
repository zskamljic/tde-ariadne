#pragma once

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// What a program asks for when it lets the user pick files, through the FileChooser portal of
// xdg-desktop-portal, and what it hears back.
namespace ariadne::chooser {

// A pattern files match by: a glob over their name, or a MIME type they are of.
struct FilterRule {
    enum Kind : uint { Glob = 0, MimeType = 1 };
    uint kind = Glob;
    QString pattern;

    bool operator==(const FilterRule&) const = default;
};

// Files of one kind, as "Images" or "All Files".
struct Filter {
    QString name;
    QList<FilterRule> rules;

    // Whether a file of this name and MIME type is of the kind.
    bool matches(const QString& fileName, const QString& mimeType) const;
    bool operator==(const Filter&) const = default;
};

// More the program wants to know, as which encoding to save in: one of `options`, or yes or
// no when there are none.
struct Choice {
    QString id;
    QString label;
    QList<std::pair<QString, QString>> options; // id, label
    QString selected; // an option's id, or "true" or "false"

    bool operator==(const Choice&) const = default;
};

enum class Mode { Open, Save, SaveFiles };

struct Request {
    Mode mode = Mode::Open;
    QString title;
    QString acceptLabel;
    bool multiple = false;
    bool directory = false; // folders are picked, not files
    QList<Filter> filters;
    int filter = -1; // of filters, the one picked at first
    QList<Choice> choices;
    QString name; // to save as
    QString folder; // to start in, empty for the usual one
    QStringList files; // SaveFiles: the names of the files to be saved

    // The request as the portal hands it over.
    static Request fromOptions(Mode mode, const QString& title, const QVariantMap& options);
};

struct Answer {
    QList<QUrl> urls;
    QList<Choice> choices; // with what was picked
    int filter = -1;
};

// The results the portal hands back for `answer` to `request`.
QVariantMap results(const Request& request, const Answer& answer);

// Makes the types above known to Qt D-Bus; called once before talking to the portal.
void registerTypes();

QDBusArgument& operator<<(QDBusArgument& argument, const FilterRule& rule);
const QDBusArgument& operator>>(const QDBusArgument& argument, FilterRule& rule);
QDBusArgument& operator<<(QDBusArgument& argument, const Filter& filter);
const QDBusArgument& operator>>(const QDBusArgument& argument, Filter& filter);
QDBusArgument& operator<<(QDBusArgument& argument, const Choice& choice);
const QDBusArgument& operator>>(const QDBusArgument& argument, Choice& choice);

} // namespace ariadne::chooser

Q_DECLARE_METATYPE(ariadne::chooser::FilterRule)
Q_DECLARE_METATYPE(ariadne::chooser::Filter)
Q_DECLARE_METATYPE(ariadne::chooser::Choice)
