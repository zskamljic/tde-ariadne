#pragma once

#include "DirectoryListing.hpp"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <expected>
#include <functional>
#include <optional>
#include <vector>

// Phones, cameras and network shares, which gvfs mounts and shows as ordinary folders under
// /run/user/<uid>/gvfs. Ariadne talks to it through the gio command line tool.
namespace ariadne::gvfs {

bool isAvailable();

struct Location {
    QString name;
    QString uri; // mtp://…, smb://server/share/, sftp://host/, …
    QString iconName;
    bool mounted = false;
    QString localPath; // where a mounted location appears as a folder

    bool operator==(const Location&) const = default;
};

// The phones, cameras and network locations in the output of `gio mount -li`, leaving out
// the drives, which UDisks2 already reports.
QList<Location> parseMountList(const QString& output);

// Runs gio for the current locations, with the folders of the mounted ones. Blocking.
QList<Location> scan();

std::expected<void, QString> unmount(const QString& uri);

// A question gio asks while mounting: a user name, password or domain, or a multiple choice
// (such as whether to trust an unknown server).
struct Prompt {
    enum class Kind { User, Domain, Password, Choice };
    Kind kind;
    QString defaultValue;
    QString question; // for choices
    QStringList choices; // for choices
};

// Mounts a location, handing gio's questions to `answer` as they come; an empty answer
// cancels. Finishes with the folder the location appears as, or an error.
class MountOperation : public QObject {
    Q_OBJECT

public:
    using Answer = std::function<std::optional<QString>(const Prompt&)>;

    MountOperation(QString uri, Answer answer, QObject* parent = nullptr);
    void start();

signals:
    void finished(const std::expected<QString, QString>& result);

private:
    void readOutput();

    QString m_uri;
    Answer m_answer;
    QProcess m_process;
    QByteArray m_output;
    bool m_cancelled = false;
};

// Whether `path` is inside the folder where gvfs shows its locations (/run/user/<uid>/gvfs).
bool isGvfsPath(const QString& path);

// One line of `gio list -l -u -a …` output in `directory` (shown as `directoryPath` on disk),
// as the entry it lists.
std::optional<FileEntry> parseListLine(
    const QString& line, const QUrl& directory, const QString& directoryPath, const QMimeDatabase& mimeDatabase);

// Lists a folder of a gvfs location through gio, which hands out entries as the phone or
// server sends them. Its FUSE folder only answers once it has all of them, which for a few
// thousand photos on a phone takes minutes.
class Listing : public QObject {
    Q_OBJECT

public:
    explicit Listing(QUrl location, QObject* parent = nullptr);
    ~Listing() override;

    void start();
    // Everything listed so far.
    const std::vector<FileEntry>& entries() const { return m_entries; }

signals:
    // Entries listed since the last time, a few at once.
    void found(const std::vector<ariadne::FileEntry>& entries);
    void finished(const std::expected<void, QString>& result);

private:
    void readOutput();
    void flush();

    QUrl m_location;
    QString m_path;
    QProcess m_process;
    QByteArray m_pending; // an incomplete line
    QString m_errors;
    QMimeDatabase m_mimeDatabase;
    std::vector<FileEntry> m_entries;
    std::size_t m_reported = 0;
    QTimer m_flushTimer;
};

// Turns what gio printed while waiting for an answer into the prompt it asks, if it asks one.
std::optional<Prompt> parsePrompt(const QString& pending);

} // namespace ariadne::gvfs
