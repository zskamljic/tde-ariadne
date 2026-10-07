#include "FileChooserPortal.hpp"

#include "FilePicker.hpp"
#include "ForeignParent.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

// What the portal hears back: picked, given up on, or ended some other way.
enum Response : uint { Picked = 0, Cancelled = 1, Ended = 2 };

// The portal's handle on one request, through which it may close the window early. It holds
// on to the window of the program that asked while the picker is shown.
class Request : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    Request(FilePicker* picker, const QString& parentWindow)
        : QObject(picker)
        , m_picker(picker)
        , m_parent(parentWindow)
    {
    }

public slots:
    void Close()
    {
        if (m_picker)
            m_picker->close();
    }

private:
    QPointer<FilePicker> m_picker;
    ForeignParent m_parent;
};

} // namespace

FileChooserPortal::FileChooserPortal(Application& app, QObject* parent)
    : QObject(parent)
    , m_app(app)
{
    chooser::registerTypes();
}

bool FileChooserPortal::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->registerService(QString::fromLatin1(ServiceName)).value())
        return false;
    return bus.registerObject(QString::fromLatin1(ObjectPath), this, QDBusConnection::ExportAllSlots);
}

uint FileChooserPortal::OpenFile(const QDBusObjectPath& handle, const QString&, const QString& parentWindow,
    const QString& title, const QVariantMap& options, QVariantMap&)
{
    pick(chooser::Mode::Open, handle, parentWindow, title, options);
    return Ended;
}

uint FileChooserPortal::SaveFile(const QDBusObjectPath& handle, const QString&, const QString& parentWindow,
    const QString& title, const QVariantMap& options, QVariantMap&)
{
    pick(chooser::Mode::Save, handle, parentWindow, title, options);
    return Ended;
}

uint FileChooserPortal::SaveFiles(const QDBusObjectPath& handle, const QString&, const QString& parentWindow,
    const QString& title, const QVariantMap& options, QVariantMap&)
{
    pick(chooser::Mode::SaveFiles, handle, parentWindow, title, options);
    return Ended;
}

// The answer waits for the window: the call is replied to once it is done.
void FileChooserPortal::pick(chooser::Mode mode, const QDBusObjectPath& handle, const QString& parentWindow,
    const QString& title, const QVariantMap& options)
{
    setDelayedReply(true);
    const QDBusMessage call = message();
    const chooser::Request request = chooser::Request::fromOptions(mode, title, options);

    auto* picker = new FilePicker(m_app, request);
    QDBusConnection bus = connection();
    bus.registerObject(handle.path(), new Request(picker, parentWindow), QDBusConnection::ExportAllSlots);
    ++m_open;
    connect(picker, &FilePicker::finished, this,
        [this, bus, call, request, path = handle.path()](bool picked, const chooser::Answer& answer) mutable {
            bus.unregisterObject(path);
            const uint response = picked ? Picked : Cancelled;
            bus.send(call.createReply({QVariant::fromValue(response),
                QVariant::fromValue(picked ? chooser::results(request, answer) : QVariantMap())}));
            --m_open;
        });
    picker->show();
    picker->raise();
    picker->activateWindow();
}

} // namespace ariadne

#include "FileChooserPortal.moc"
