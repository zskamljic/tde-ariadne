#pragma once

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

namespace ariadne {

struct Bookmark {
    QUrl url;
    QString label; // empty means "use the folder name"

    bool operator==(const Bookmark&) const = default;
};

// The bookmark list, stored in the GTK bookmarks file so it stays in sync with file
// choosers of other applications. External changes to the file are picked up live.
class Bookmarks : public QObject {
    Q_OBJECT

public:
    explicit Bookmarks(QString filePath = defaultFilePath(), QObject* parent = nullptr);

    static QString defaultFilePath();
    static QString displayName(const Bookmark& bookmark);

    const QList<Bookmark>& items() const { return m_items; }
    qsizetype indexOf(const QUrl& url) const;
    bool contains(const QUrl& url) const { return indexOf(url) >= 0; }

    // `index` is the position to insert before; -1 appends.
    void add(const QUrl& url, qsizetype index = -1);
    void remove(const QUrl& url);
    void rename(const QUrl& url, const QString& label);
    // Moves the bookmark at `from` so that it ends up before the bookmark currently at `to`.
    void move(qsizetype from, qsizetype to);

signals:
    void changed();

private:
    void load();
    void save();
    void watchFile();

    QString m_filePath;
    QList<Bookmark> m_items;
    QFileSystemWatcher m_watcher;
};

} // namespace ariadne
