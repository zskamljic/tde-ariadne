#pragma once

#include "Gvfs.hpp"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QFileSystemWatcher>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <expected>
#include <functional>
#include <optional>

namespace ariadne {

struct Device {
    QString id; // UDisks2 block device object path, or "gvfs:" and the location's URI
    QString label;
    QString iconName;
    QString mountPoint; // empty when not mounted
    QString driveId;
    QString backingId; // for an unlocked encrypted device, the encrypted one it comes from
    QString uri; // for phones, cameras and network locations
    bool removable = false;
    bool canEject = false;
    bool canPowerOff = false;
    bool locked = false; // encrypted, and waiting for its passphrase

    bool operator==(const Device&) const = default;
};

namespace udisks {

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;

// The devices worth showing among UDisks2's objects: file systems that are not part of the
// running system, and encrypted volumes that are still locked.
QList<Device> devicesFromObjects(const ObjectMap& objects);

} // namespace udisks

// Mounted and mountable devices: drives from UDisks2 (falling back to the mount table
// without it), and phones, cameras and network locations from gvfs.
class DeviceMonitor : public QObject {
    Q_OBJECT

public:
    using MountCallback = std::function<void(std::expected<QString, QString>)>; // mount point or error
    using ActionCallback = std::function<void(std::expected<void, QString>)>;

    explicit DeviceMonitor(QObject* parent = nullptr);

    const QList<Device>& devices() const { return m_devices; }
    // A copy, as the list changes whenever drives come and go.
    std::optional<Device> find(const QString& id) const;

    // Network locations may ask for a user name and password through `answer`.
    void mount(const QString& id, MountCallback done, gvfs::MountOperation::Answer answer = {});
    // Unlocks an encrypted device with its passphrase and mounts what it holds.
    void unlock(const QString& id, const QString& passphrase, MountCallback done);
    // Unmounts, locking encrypted devices again.
    void unmount(const QString& id, ActionCallback done);
    // Unmounts, then ejects or powers off the drive so it can be removed safely.
    void eject(const QString& id, ActionCallback done);
    // Mounts a network location given by its address, such as smb://server/share.
    void connectTo(const QString& uri, gvfs::MountOperation::Answer answer, MountCallback done);

signals:
    void changed();

private slots:
    void scheduleRefresh();

private:
    void refresh();
    void refreshFromMountTable();
    void refreshNetwork();
    void watchNetwork();
    void publish();
    void call(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments,
        std::function<void(const QDBusMessage&)> done);
    void mountNetwork(const QString& uri, gvfs::MountOperation::Answer answer, MountCallback done);

    QDBusConnection m_bus;
    QTimer m_refreshTimer;
    QTimer m_networkTimer;
    QFileSystemWatcher m_networkWatcher;
    bool m_scanningNetwork = false;
    bool m_scanNetworkAgain = false;
    QList<Device> m_drives;
    QList<Device> m_network;
    QList<Device> m_devices;
};

} // namespace ariadne
