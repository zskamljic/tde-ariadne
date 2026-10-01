#include "core/DeviceMonitor.hpp"
#include "core/Gvfs.hpp"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace ariadne;

namespace {

// The shape of `gio mount -li` with a drive from UDisks2, a mounted phone and share,
// and a phone that is plugged in but not mounted yet.
const auto MountList = uR"(Drive(0): Samsung SSD
  Type: GProxyDrive (GProxyVolumeMonitorUDisks2)
  themed icons:  [drive-harddisk]  [drive]
  Volume(0): 640 GB Volume
    Type: GProxyVolume (GProxyVolumeMonitorUDisks2)
    Mount(0): 640 GB Volume -> file:///mnt/640_disk
      Type: GProxyMount (GProxyVolumeMonitorUDisks2)
Volume(0): Pixel 8
  Type: GProxyVolume (GProxyVolumeMonitorMTP)
  activation_root=mtp://Google_Pixel_8_ABC/
  themed icons:  [phone]  [phone-symbolic]
Volume(1): Galaxy S24
  Type: GProxyVolume (GProxyVolumeMonitorMTP)
  activation_root=mtp://SAMSUNG_Galaxy_XYZ/
  themed icons:  [phone]
  Mount(0): Galaxy S24 -> mtp://SAMSUNG_Galaxy_XYZ/
    Type: GProxyShadowMount (GProxyVolumeMonitorMTP)
Mount(0): mtp -> mtp://SAMSUNG_Galaxy_XYZ/
  Type: GDaemonMount
  themed icons:  [multimedia-player]
Mount(1): share on nas -> smb://nas/share/
  Type: GDaemonMount
  themed icons:  [folder-remote]  [folder]
Mount(2): home -> file:///home
  Type: GUnixMount
)"_s;

udisks::InterfaceMap block(const QVariantMap& properties, const QString& usage)
{
    QVariantMap values = properties;
    values.insert(u"IdUsage"_s, usage);
    return {{u"org.freedesktop.UDisks2.Block"_s, values}};
}

QDBusObjectPath path(const QString& name)
{
    return QDBusObjectPath(u"/org/freedesktop/UDisks2/"_s + name);
}

} // namespace

class TestDevices : public QObject {
    Q_OBJECT

private slots:
    void mountList()
    {
        const QList<gvfs::Location> locations = gvfs::parseMountList(MountList);
        QCOMPARE(locations.size(), 3);

        QCOMPARE(locations[0].name, u"Galaxy S24"_s);
        QCOMPARE(locations[0].uri, u"mtp://SAMSUNG_Galaxy_XYZ/"_s);
        QCOMPARE(locations[0].iconName, u"phone"_s);
        QVERIFY(locations[0].mounted);

        QCOMPARE(locations[1].name, u"share on nas"_s);
        QCOMPARE(locations[1].uri, u"smb://nas/share/"_s);
        QCOMPARE(locations[1].iconName, u"folder-remote"_s);
        QVERIFY(locations[1].mounted);

        QCOMPARE(locations[2].name, u"Pixel 8"_s);
        QCOMPARE(locations[2].iconName, u"phone"_s);
        QVERIFY(!locations[2].mounted);
    }

    void prompts()
    {
        const auto user
            = gvfs::parsePrompt(u"Authentication Required\nEnter user and password for share:\nUser [zan]: "_s);
        QVERIFY(user);
        QCOMPARE(user->kind, gvfs::Prompt::Kind::User);
        QCOMPARE(user->defaultValue, u"zan"_s);

        const auto password = gvfs::parsePrompt(u"Password: "_s);
        QVERIFY(password);
        QCOMPARE(password->kind, gvfs::Prompt::Kind::Password);

        QVERIFY(!gvfs::parsePrompt(u"Password: still typing"_s));
        QVERIFY(!gvfs::parsePrompt(u"Mounting…\n"_s));
    }

    void listLines()
    {
        const QMimeDatabase mimeDatabase;
        const QUrl folder = QUrl::fromLocalFile(u"/run/user/1000/gvfs/mtp:host=Phone/DCIM"_s);
        const QString path = folder.toLocalFile();

        const auto photo = gvfs::parseListLine(u"mtp://Phone/DCIM/PXL%2020260908.jpg\t2345678\t(regular)\t"
                                               u"standard::content-type=image/jpeg time::modified=1790611420"_s,
            folder, path, mimeDatabase);
        QVERIFY(photo);
        QCOMPARE(photo->name, u"PXL 20260908.jpg"_s);
        QCOMPARE(photo->path, path + u"/PXL 20260908.jpg"_s);
        QCOMPARE(photo->size, 2345678);
        QCOMPARE(photo->mimeType, u"image/jpeg"_s);
        QCOMPARE(photo->modified, QDateTime::fromSecsSinceEpoch(1790611420));
        QVERIFY(photo->isRemote);
        QVERIFY(!photo->isDir);
        QVERIFY(!photo->isHidden);

        const auto folderEntry = gvfs::parseListLine(
            u"mtp://Phone/DCIM/.thumbnails\t0\t(directory)\tstandard::content-type=inode/directory "
            u"standard::is-hidden=TRUE"_s,
            folder, path, mimeDatabase);
        QVERIFY(folderEntry);
        QVERIFY(folderEntry->isDir);
        QVERIFY(folderEntry->isHidden);
        QCOMPARE(folderEntry->childCount, -1);

        // Without a content type, the name decides.
        const auto note
            = gvfs::parseListLine(u"mtp://Phone/DCIM/notes.txt\t12\t(regular)"_s, folder, path, mimeDatabase);
        QVERIFY(note);
        QCOMPARE(note->mimeType, u"text/plain"_s);

        QVERIFY(!gvfs::parseListLine(u"garbage"_s, folder, path, mimeDatabase));
    }

    void encryptedDevices()
    {
        const QString drive = u"/org/freedesktop/UDisks2/drives/Stick"_s;
        udisks::ObjectMap objects;
        objects.insert(QDBusObjectPath(drive),
            {{u"org.freedesktop.UDisks2.Drive"_s, {{u"Removable"_s, true}, {u"Ejectable"_s, true}}}});

        // Locked: only the encrypted volume is there.
        udisks::InterfaceMap locked
            = block({{u"Drive"_s, QVariant::fromValue(QDBusObjectPath(drive))}, {u"Size"_s, quint64(16'000'000'000)}},
                u"crypto"_s);
        locked.insert(u"org.freedesktop.UDisks2.Encrypted"_s,
            {{u"CleartextDevice"_s, QVariant::fromValue(QDBusObjectPath(u"/"_s))}});
        objects.insert(path(u"block_devices/sdd1"_s), locked);

        QList<Device> devices = udisks::devicesFromObjects(objects);
        QCOMPARE(devices.size(), 1);
        QVERIFY(devices[0].locked);
        QCOMPARE(devices[0].label, u"16 GB Encrypted Volume"_s);
        QVERIFY(devices[0].removable);
        QCOMPARE(devices[0].iconName, u"drive-removable-media"_s);

        // Unlocked: the volume it opens into takes its place, on the same drive.
        objects[path(u"block_devices/sdd1"_s)][u"org.freedesktop.UDisks2.Encrypted"_s][u"CleartextDevice"_s]
            = QVariant::fromValue(path(u"block_devices/dm_2d0"_s));
        udisks::InterfaceMap cleartext
            = block({{u"Drive"_s, QVariant::fromValue(QDBusObjectPath(u"/"_s))}, {u"IdLabel"_s, u"Secrets"_s},
                        {u"CryptoBackingDevice"_s, QVariant::fromValue(path(u"block_devices/sdd1"_s))}},
                u"filesystem"_s);
        cleartext.insert(u"org.freedesktop.UDisks2.Filesystem"_s, {{u"MountPoints"_s, QVariant()}});
        objects.insert(path(u"block_devices/dm_2d0"_s), cleartext);

        devices = udisks::devicesFromObjects(objects);
        QCOMPARE(devices.size(), 1);
        QVERIFY(!devices[0].locked);
        QCOMPARE(devices[0].label, u"Secrets"_s);
        QCOMPARE(devices[0].backingId, path(u"block_devices/sdd1"_s).path());
        QCOMPARE(devices[0].driveId, drive);
        QVERIFY(devices[0].canEject);
    }
};

QTEST_GUILESS_MAIN(TestDevices)
#include "tst_devices.moc"
