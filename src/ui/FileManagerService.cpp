#include "FileManagerService.hpp"

#include "Application.hpp"
#include "MainWindow.hpp"
#include "core/Location.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <optional>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

std::optional<QUrl> toLocation(const QString& uri)
{
    return location::fromUserInput(uri, location::home());
}

// The window opened next may take focus: on Wayland, only with a token from whoever asked.
void useActivationToken(const QString& startupId)
{
    if (!startupId.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", startupId.toUtf8());
}

} // namespace

FileManagerService::FileManagerService(Application& app, QObject* parent)
    : QObject(parent)
    , m_app(app)
{
}

bool FileManagerService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return true; // nothing to share with; run on our own
    QDBusConnectionInterface* names = bus.interface();
    if (!names->registerService(QString::fromLatin1(ServiceName)).value())
        return false;
    bus.registerObject(QString::fromLatin1(ObjectPath), this, QDBusConnection::ExportAllSlots);
    // Nautilus may hold it while it runs; then Ariadne takes over once it is gone.
    names->registerService(QString::fromLatin1(FileManagerName), QDBusConnectionInterface::QueueService,
        QDBusConnectionInterface::AllowReplacement);
    return true;
}

void FileManagerService::unregisterFromBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    bus.unregisterObject(QString::fromLatin1(ObjectPath));
    bus.interface()->unregisterService(QString::fromLatin1(FileManagerName));
    bus.interface()->unregisterService(QString::fromLatin1(ServiceName));
}

bool FileManagerService::isRunning()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface()->isServiceRegistered(QString::fromLatin1(ServiceName)).value();
}

void FileManagerService::takeOverFromNautilus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    const auto pid = bus.interface()->servicePid(QString::fromLatin1(FileManagerName));
    if (!pid.isValid())
        return;
    QFile comm(u"/proc/%1/comm"_s.arg(pid.value()));
    if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "nautilus")
        QProcess::startDetached(u"nautilus"_s, {u"-q"_s});
}

bool FileManagerService::forwardToRunning(const QStringList& folders, const QStringList& items)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!isRunning())
        return false;
    const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN", qEnvironmentVariable("DESKTOP_STARTUP_ID"));
    const auto call = [&](const QString& method, const QStringList& uris) {
        auto message = QDBusMessage::createMethodCall(QString::fromLatin1(ServiceName), QString::fromLatin1(ObjectPath),
            QString::fromLatin1(FileManagerName), method);
        message << uris << token;
        return bus.call(message, QDBus::Block, 10'000).type() == QDBusMessage::ReplyMessage;
    };
    // A token lets one window take focus; the folders come first.
    bool ok = true;
    if (!folders.isEmpty())
        ok = call(u"ShowFolders"_s, folders);
    if (ok && !items.isEmpty())
        ok = call(u"ShowItems"_s, items);
    return ok;
}

void FileManagerService::ShowFolders(const QStringList& uris, const QString& startupId)
{
    useActivationToken(startupId);
    for (const QString& uri : uris) {
        if (const auto location = toLocation(uri))
            m_app.openWindow(*location);
    }
}

void FileManagerService::ShowItems(const QStringList& uris, const QString& startupId)
{
    useActivationToken(startupId);
    // One window per folder, with the first of its items selected.
    QStringList shownFolders;
    for (const QString& uri : uris) {
        const auto location = toLocation(uri);
        if (!location)
            continue;
        const QUrl folder = location::parent(*location).value_or(*location);
        if (shownFolders.contains(folder.toString()))
            continue;
        shownFolders << folder.toString();
        m_app.openWindow(folder, QFileInfo(location::localPath(*location)).fileName());
    }
}

void FileManagerService::ShowItemProperties(const QStringList& uris, const QString& startupId)
{
    useActivationToken(startupId);
    QStringList paths;
    for (const QString& uri : uris) {
        if (const auto location = toLocation(uri); location && location::isLocal(*location))
            paths << location::localPath(*location);
    }
    if (paths.isEmpty())
        return;
    const QString folder = QFileInfo(paths.front()).absolutePath();
    MainWindow* window = m_app.openWindow(location::fromLocalPath(folder), QFileInfo(paths.front()).fileName());
    window->showPropertiesOfPaths(paths);
}

} // namespace ariadne
