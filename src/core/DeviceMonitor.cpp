#include "DeviceMonitor.hpp"

#include <QCollator>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

using ariadne::udisks::InterfaceMap;
using ariadne::udisks::ObjectMap;

namespace ariadne {
namespace {

constexpr auto Service = "org.freedesktop.UDisks2"_L1;
constexpr auto ManagerPath = "/org/freedesktop/UDisks2"_L1;
constexpr auto ObjectManagerInterface = "org.freedesktop.DBus.ObjectManager"_L1;
constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties"_L1;
constexpr auto BlockInterface = "org.freedesktop.UDisks2.Block"_L1;
constexpr auto FilesystemInterface = "org.freedesktop.UDisks2.Filesystem"_L1;
constexpr auto PartitionInterface = "org.freedesktop.UDisks2.Partition"_L1;
constexpr auto DriveInterface = "org.freedesktop.UDisks2.Drive"_L1;
constexpr auto EncryptedInterface = "org.freedesktop.UDisks2.Encrypted"_L1;

// Mounting may wait for a polkit password prompt, so allow plenty of time.
constexpr int CallTimeoutMs = 5 * 60 * 1000;

// Partitions that are part of how the machine boots, never interesting to browse.
bool isHiddenPartitionType(const QString& type)
{
    static const QStringList hidden {
        u"c12a7328-f81f-11d2-ba4b-00a0c93ec93b"_s, // EFI system partition
        u"21686148-6449-6e6f-744e-656564454649"_s, // BIOS boot
        u"e3c9e316-0b5c-4db8-817d-f92df00215ae"_s, // Microsoft reserved
        u"de94bba4-06d1-4d40-a16a-bfd50179d6ac"_s, // Windows recovery
        u"0xef"_s, // EFI system partition (MBR)
        u"0x27"_s, // Windows recovery (MBR)
    };
    return hidden.contains(type.toLower());
}

bool isSystemMountPoint(const QString& path)
{
    static const QStringList system {
        u"/"_s, u"/boot"_s, u"/efi"_s, u"/home"_s, u"/usr"_s, u"/var"_s, u"/tmp"_s, u"/opt"_s, u"/srv"_s, u"/root"_s};
    return system.contains(path) || path.startsWith(u"/boot/") || path.startsWith(u"/usr/")
        || path.startsWith(u"/var/");
}

QList<QByteArray> byteArrayList(const QVariant& value)
{
    QList<QByteArray> result;
    if (value.canConvert<QDBusArgument>())
        value.value<QDBusArgument>() >> result;
    else
        result = value.value<QList<QByteArray>>();
    for (QByteArray& bytes : result) {
        if (bytes.endsWith('\0'))
            bytes.chop(1);
    }
    return result;
}

QString sizeText(const QVariantMap& block)
{
    return QLocale().formattedDataSize(
        static_cast<qint64>(block.value(u"Size"_s).toULongLong()), 0, QLocale::DataSizeSIFormat);
}

QString objectPath(const QVariant& value)
{
    const QString path = value.value<QDBusObjectPath>().path();
    return path == u"/" ? QString() : path;
}

// What a device's drive says about removing it.
void describeDrive(Device& device, const ObjectMap& objects)
{
    const QVariantMap drive = objects.value(QDBusObjectPath(device.driveId)).value(DriveInterface);
    device.removable = drive.value(u"Removable"_s).toBool() || drive.value(u"MediaRemovable"_s).toBool();
    device.canEject = drive.value(u"Ejectable"_s).toBool();
    device.canPowerOff = drive.value(u"CanPowerOff"_s).toBool();
}

QString labelOf(const QVariantMap& block)
{
    QString label = block.value(u"HintName"_s).toString();
    if (label.isEmpty())
        label = block.value(u"IdLabel"_s).toString();
    return label;
}

QString errorText(const QDBusMessage& reply)
{
    const QString message = reply.errorMessage();
    return message.isEmpty() ? reply.errorName() : message;
}

void sortByLabel(QList<Device>& devices)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(
        devices, [&](const Device& a, const Device& b) { return collator.compare(a.label, b.label) < 0; });
}

} // namespace

namespace udisks {

QList<Device> devicesFromObjects(const ObjectMap& objects)
{
    QList<Device> devices;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const InterfaceMap& interfaces = it.value();
        if (!interfaces.contains(BlockInterface))
            continue;
        const QVariantMap& block = interfaces[BlockInterface];
        if (block.value(u"HintIgnore"_s).toBool())
            continue;
        if (interfaces.contains(PartitionInterface)
            && isHiddenPartitionType(interfaces[PartitionInterface].value(u"Type"_s).toString()))
            continue;

        Device device;
        device.id = it.key().path();
        device.driveId = objectPath(block.value(u"Drive"_s));
        device.iconName = block.value(u"HintIconName"_s).toString();

        // A locked encrypted volume; once unlocked, the device it opens into is shown instead.
        if (interfaces.contains(EncryptedInterface) && block.value(u"IdUsage"_s).toString() == u"crypto") {
            if (!objectPath(interfaces[EncryptedInterface].value(u"CleartextDevice"_s)).isEmpty())
                continue;
            describeDrive(device, objects);
            device.locked = true;
            device.label = labelOf(block);
            if (device.label.isEmpty())
                device.label = u"%1 Encrypted Volume"_s.arg(sizeText(block));
            if (device.iconName.isEmpty())
                device.iconName = device.removable ? u"drive-removable-media"_s : u"drive-harddisk"_s;
            devices << device;
            continue;
        }

        if (!interfaces.contains(FilesystemInterface) || block.value(u"IdUsage"_s).toString() != u"filesystem")
            continue;
        const QList<QByteArray> mountPoints = byteArrayList(interfaces[FilesystemInterface].value(u"MountPoints"_s));
        if (std::ranges::any_of(
                mountPoints, [](const QByteArray& path) { return isSystemMountPoint(QString::fromLocal8Bit(path)); }))
            continue;
        if (!mountPoints.isEmpty())
            device.mountPoint = QString::fromLocal8Bit(mountPoints.first());

        device.label = labelOf(block);
        // An unlocked encrypted volume: its drive and name belong to the encrypted device.
        device.backingId = objectPath(block.value(u"CryptoBackingDevice"_s));
        if (!device.backingId.isEmpty()) {
            const QVariantMap backing = objects.value(QDBusObjectPath(device.backingId)).value(BlockInterface);
            if (device.driveId.isEmpty())
                device.driveId = objectPath(backing.value(u"Drive"_s));
            if (device.label.isEmpty())
                device.label = labelOf(backing);
        }
        describeDrive(device, objects);
        if (device.label.isEmpty())
            device.label = u"%1 Volume"_s.arg(sizeText(block));
        if (device.iconName.isEmpty())
            device.iconName = device.removable ? u"drive-removable-media"_s : u"drive-harddisk"_s;
        devices << device;
    }
    sortByLabel(devices);
    return devices;
}

} // namespace udisks

DeviceMonitor::DeviceMonitor(QObject* parent)
    : QObject(parent)
    , m_bus(QDBusConnection::systemBus())
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DeviceMonitor::refresh);
    // Phones take a moment after being plugged in before gvfs knows them.
    m_networkTimer.setSingleShot(true);
    m_networkTimer.setInterval(1500);
    connect(&m_networkTimer, &QTimer::timeout, this, &DeviceMonitor::refreshNetwork);
    connect(&m_networkWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        watchNetwork();
        m_networkTimer.start();
    });

    if (m_bus.isConnected()) {
        m_bus.connect(
            Service, ManagerPath, ObjectManagerInterface, u"InterfacesAdded"_s, this, SLOT(scheduleRefresh()));
        m_bus.connect(
            Service, ManagerPath, ObjectManagerInterface, u"InterfacesRemoved"_s, this, SLOT(scheduleRefresh()));
        m_bus.connect(Service, QString(), PropertiesInterface, u"PropertiesChanged"_s, this, SLOT(scheduleRefresh()));
    }
    watchNetwork();
    refresh();
    refreshNetwork();
}

std::optional<Device> DeviceMonitor::find(const QString& id) const
{
    const auto it = std::ranges::find(m_devices, id, &Device::id);
    return it == m_devices.end() ? std::nullopt : std::optional(*it);
}

void DeviceMonitor::scheduleRefresh()
{
    m_refreshTimer.start();
}

void DeviceMonitor::refresh()
{
    if (!m_bus.isConnected()) {
        refreshFromMountTable();
        return;
    }

    const auto message
        = QDBusMessage::createMethodCall(Service, ManagerPath, ObjectManagerInterface, u"GetManagedObjects"_s);
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<ObjectMap> reply = *watcher;
        if (reply.isError()) {
            refreshFromMountTable();
            return;
        }
        m_drives = udisks::devicesFromObjects(reply.value());
        publish();
    });
}

void DeviceMonitor::refreshFromMountTable()
{
    QList<Device> devices;
    for (const QStorageInfo& storage : QStorageInfo::mountedVolumes()) {
        const QString path = storage.rootPath();
        if (!storage.isValid() || !storage.isReady()
            || !(path.startsWith(u"/run/media/") || path.startsWith(u"/media/") || path.startsWith(u"/mnt/")))
            continue;
        Device device;
        device.id = u"mount:"_s + path;
        device.label = storage.displayName();
        device.iconName = u"drive-harddisk"_s;
        device.mountPoint = path;
        devices << device;
    }
    m_drives = std::move(devices);
    publish();
}

void DeviceMonitor::watchNetwork()
{
    // gvfs mounts appear in its folder; phones and cameras come and go on the USB bus.
    QStringList paths {QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + u"/gvfs"_s};
    for (const QString& bus : QDir(u"/dev/bus/usb"_s).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        paths << u"/dev/bus/usb/"_s + bus;
    for (const QString& path : std::as_const(paths)) {
        if (QFileInfo(path).isDir() && !m_networkWatcher.directories().contains(path))
            m_networkWatcher.addPath(path);
    }
}

void DeviceMonitor::refreshNetwork()
{
    if (!gvfs::isAvailable())
        return;
    if (m_scanningNetwork) {
        m_scanNetworkAgain = true;
        return;
    }
    m_scanningNetwork = true;
    auto* watcher = new QFutureWatcher<QList<gvfs::Location>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_scanningNetwork = false;
        QList<Device> devices;
        for (const gvfs::Location& location : watcher->result()) {
            Device device;
            device.id = u"gvfs:"_s + location.uri;
            device.uri = location.uri;
            device.label = location.name;
            device.iconName = location.iconName;
            device.mountPoint = location.mounted ? location.localPath : QString();
            device.removable = location.mounted; // offers unmounting
            devices << device;
        }
        sortByLabel(devices);
        m_network = std::move(devices);
        publish();
        if (std::exchange(m_scanNetworkAgain, false))
            refreshNetwork();
    });
    watcher->setFuture(QtConcurrent::run(&gvfs::scan));
}

void DeviceMonitor::publish()
{
    QList<Device> devices = m_drives + m_network;
    if (devices == m_devices)
        return;
    m_devices = std::move(devices);
    emit changed();
}

void DeviceMonitor::call(const QString& path, const QString& interface, const QString& method,
    const QVariantList& arguments, std::function<void(const QDBusMessage&)> done)
{
    auto message = QDBusMessage::createMethodCall(Service, path, interface, method);
    message.setArguments(arguments);
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        done(watcher->reply());
    });
}

void DeviceMonitor::mountNetwork(const QString& uri, gvfs::MountOperation::Answer answer, MountCallback done)
{
    auto* operation = new gvfs::MountOperation(uri, std::move(answer), this);
    connect(operation, &gvfs::MountOperation::finished, this,
        [this, operation, done = std::move(done)](const std::expected<QString, QString>& result) {
            operation->deleteLater();
            m_networkTimer.start();
            done(result);
        });
    operation->start();
}

void DeviceMonitor::mount(const QString& id, MountCallback done, gvfs::MountOperation::Answer answer)
{
    const std::optional<Device> device = find(id);
    if (!device) {
        done(std::unexpected(u"The device is no longer available."_s));
        return;
    }
    if (!device->mountPoint.isEmpty()) {
        done(device->mountPoint);
        return;
    }
    if (device->locked) {
        done(std::unexpected(u"The device is locked."_s));
        return;
    }
    if (!device->uri.isEmpty()) {
        mountNetwork(device->uri, std::move(answer), std::move(done));
        return;
    }
    if (id.startsWith(u"mount:")) {
        done(std::unexpected(u"Mounting needs UDisks2, which is not running."_s));
        return;
    }
    call(id, FilesystemInterface, u"Mount"_s, {QVariantMap {}}, [done = std::move(done)](const QDBusMessage& reply) {
        if (reply.type() == QDBusMessage::ErrorMessage)
            done(std::unexpected(errorText(reply)));
        else
            done(reply.arguments().value(0).toString());
    });
}

void DeviceMonitor::unlock(const QString& id, const QString& passphrase, MountCallback done)
{
    call(id, EncryptedInterface, u"Unlock"_s, {passphrase, QVariantMap {}},
        [this, done = std::move(done)](const QDBusMessage& reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                done(std::unexpected(errorText(reply)));
                return;
            }
            // What the volume holds, ready to be mounted.
            const QString cleartext = reply.arguments().value(0).value<QDBusObjectPath>().path();
            call(cleartext, FilesystemInterface, u"Mount"_s, {QVariantMap {}}, [done](const QDBusMessage& mounted) {
                if (mounted.type() == QDBusMessage::ErrorMessage)
                    done(std::unexpected(errorText(mounted)));
                else
                    done(mounted.arguments().value(0).toString());
            });
        });
}

void DeviceMonitor::unmount(const QString& id, ActionCallback done)
{
    const std::optional<Device> device = find(id);
    if (!device) {
        done(std::unexpected(u"The device is no longer available."_s));
        return;
    }
    if (!device->uri.isEmpty()) {
        auto* watcher = new QFutureWatcher<std::expected<void, QString>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, done = std::move(done)] {
            watcher->deleteLater();
            m_networkTimer.start();
            done(watcher->result());
        });
        watcher->setFuture(QtConcurrent::run([uri = device->uri] { return gvfs::unmount(uri); }));
        return;
    }
    if (id.startsWith(u"mount:")) {
        done(std::unexpected(u"Unmounting needs UDisks2, which is not running."_s));
        return;
    }
    call(id, FilesystemInterface, u"Unmount"_s, {QVariantMap {}},
        [this, backing = device->backingId, done = std::move(done)](const QDBusMessage& reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                done(std::unexpected(errorText(reply)));
                return;
            }
            if (backing.isEmpty()) {
                done({});
                return;
            }
            // Encrypted: lock it again, so the passphrase is needed next time.
            call(backing, EncryptedInterface, u"Lock"_s, {QVariantMap {}}, [done](const QDBusMessage& locked) {
                if (locked.type() == QDBusMessage::ErrorMessage)
                    done(std::unexpected(errorText(locked)));
                else
                    done({});
            });
        });
}

void DeviceMonitor::eject(const QString& id, ActionCallback done)
{
    const std::optional<Device> device = find(id);
    if (!device) {
        done(std::unexpected(u"The device is no longer available."_s));
        return;
    }
    if (!device->uri.isEmpty()) {
        unmount(id, std::move(done));
        return;
    }

    const QString driveId = device->driveId;
    const QString method = device->canEject ? u"Eject"_s : device->canPowerOff ? u"PowerOff"_s : QString();
    auto finishDrive = [this, driveId, method, done = std::move(done)](std::expected<void, QString> unmounted) {
        if (!unmounted || method.isEmpty() || driveId.isEmpty()) {
            done(unmounted);
            return;
        }
        call(driveId, DriveInterface, method, {QVariantMap {}}, [done](const QDBusMessage& reply) {
            if (reply.type() == QDBusMessage::ErrorMessage)
                done(std::unexpected(errorText(reply)));
            else
                done({});
        });
    };

    if (device->mountPoint.isEmpty())
        finishDrive({});
    else
        unmount(id, std::move(finishDrive));
}

void DeviceMonitor::connectTo(const QString& uri, gvfs::MountOperation::Answer answer, MountCallback done)
{
    mountNetwork(uri, std::move(answer), std::move(done));
}

} // namespace ariadne
