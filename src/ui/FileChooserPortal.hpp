#pragma once

#include "core/FileChooser.hpp"

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

namespace ariadne {

class Application;

// The FileChooser of xdg-desktop-portal for TDE: programs that let the user pick files, and
// those in a sandbox, have them picked in a window of Ariadne. Its slots are the portal's
// D-Bus methods; each is answered once its window is done.
class FileChooserPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")

public:
    static constexpr const char* ServiceName = "org.freedesktop.impl.portal.desktop.tde";
    static constexpr const char* ObjectPath = "/org/freedesktop/portal/desktop";

    explicit FileChooserPortal(Application& app, QObject* parent = nullptr);

    bool registerOnBus();
    // Whether a window is picking files, which keeps Ariadne running.
    bool isBusy() const { return m_open > 0; }

public slots:
    uint OpenFile(const QDBusObjectPath& handle, const QString& appId, const QString& parentWindow,
        const QString& title, const QVariantMap& options, QVariantMap& results);
    uint SaveFile(const QDBusObjectPath& handle, const QString& appId, const QString& parentWindow,
        const QString& title, const QVariantMap& options, QVariantMap& results);
    uint SaveFiles(const QDBusObjectPath& handle, const QString& appId, const QString& parentWindow,
        const QString& title, const QVariantMap& options, QVariantMap& results);

private:
    void pick(chooser::Mode mode, const QDBusObjectPath& handle, const QString& title, const QVariantMap& options);

    Application& m_app;
    int m_open = 0;
};

} // namespace ariadne
