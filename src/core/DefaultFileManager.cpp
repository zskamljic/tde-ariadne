#include "DefaultFileManager.hpp"

#include "Applications.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace ariadne::defaults {
namespace {

constexpr auto DesktopId = "ariadne.desktop";

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

std::expected<void, QString> writeFile(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0 || !file.commit())
        return std::unexpected(u"Could not write %1: %2"_s.arg(path, file.errorString()));
    return {};
}

bool startsAriadne(const QString& path)
{
    const QString text = readFile(path);
    return text.contains(u"ariadne --"_s);
}

} // namespace

QString fileManagerServicePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + u"/dbus-1/services/org.freedesktop.FileManager1.service"_s;
}

QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u"/autostart/ariadne.desktop"_s;
}

Status status()
{
    Status status;
    const Applications applications = Applications::load();
    const DesktopApp* folders = applications.defaultFor(u"inode/directory"_s);
    status.opensFolders = folders && folders->id == QLatin1StringView(DesktopId);
    status.showsFiles = startsAriadne(fileManagerServicePath());
    status.runsAtLogin = startsAriadne(autostartPath());
    return status;
}

std::expected<void, QString> makeDefault(const QString& executable)
{
    if (auto opened = Applications::setDefault(u"inode/directory"_s, QString::fromLatin1(DesktopId)); !opened)
        return std::unexpected(u"Could not make Ariadne open folders: %1"_s.arg(opened.error()));

    // Found before Nautilus's system-wide one, so D-Bus starts Ariadne when nothing holds the name.
    const QString service
        = u"[D-BUS Service]\nName=org.freedesktop.FileManager1\nExec=%1 --service\n"_s.arg(executable);
    if (auto written = writeFile(fileManagerServicePath(), service); !written)
        return written;

    // Nautilus starts whenever a file chooser opens or the overview searches files, and takes
    // FileManager1 if it is free; an Ariadne running from login holds it first.
    const QString autostart = u"[Desktop Entry]\n"
                              u"Type=Application\n"
                              u"Name=Ariadne\n"
                              u"Comment=Keeps Ariadne ready to show folders and files\n"
                              u"Exec=%1 --background\n"
                              u"Icon=system-file-manager\n"
                              u"NoDisplay=true\n"
                              u"X-GNOME-Autostart-enabled=true\n"_s.arg(executable);
    if (auto written = writeFile(autostartPath(), autostart); !written)
        return written;

    // D-Bus only rereads service files when asked.
    if (QDBusConnection bus = QDBusConnection::sessionBus(); bus.isConnected()) {
        bus.call(QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s,
                     u"org.freedesktop.DBus"_s, u"ReloadConfig"_s),
            QDBus::Block, 5000);
    }
    return {};
}

} // namespace ariadne::defaults
