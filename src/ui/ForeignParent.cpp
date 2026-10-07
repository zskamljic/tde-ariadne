#include "ForeignParent.hpp"

#include "xdg-foreign-unstable-v2-client-protocol.h"

#include <QGuiApplication>

#include <wayland-client.h>

#include <cstring>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

template <auto destroy> struct Destroy {
    void operator()(auto* object) const { destroy(object); }
};
template <typename T, auto destroy> using Owned = std::unique_ptr<T, Destroy<destroy>>;

} // namespace

// The Wayland objects of the import, on a queue of their own so Qt's own are left alone. They
// are made in the order they are declared and so go in reverse, the queue last.
struct ForeignParent::Import {
    wl_display* display = nullptr; // Qt's connection
    Owned<wl_event_queue, wl_event_queue_destroy> queue;
    Owned<wl_display, wl_proxy_wrapper_destroy> wrapper; // the connection, on the queue
    Owned<wl_registry, wl_registry_destroy> registry;
    Owned<zxdg_importer_v2, zxdg_importer_v2_destroy> importer;
    Owned<zxdg_imported_v2, zxdg_imported_v2_destroy> imported;

    // Binds the importer once the compositor names it.
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        if (std::strcmp(interface, zxdg_importer_v2_interface.name) == 0) {
            static_cast<Import*>(data)->importer.reset(
                static_cast<zxdg_importer_v2*>(wl_registry_bind(registry, name, &zxdg_importer_v2_interface, 1)));
        }
    }
};

ForeignParent::ForeignParent(const QString& parentWindow)
{
    const QString prefix = u"wayland:"_s;
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!parentWindow.startsWith(prefix) || !wayland || !wayland->display())
        return;

    static constexpr wl_registry_listener Listener {
        .global = &Import::global,
        .global_remove = [](void*, wl_registry*, uint32_t) { },
    };
    auto import = std::make_unique<Import>();
    import->display = wayland->display();
    import->queue.reset(wl_display_create_queue(import->display));
    import->wrapper.reset(static_cast<wl_display*>(wl_proxy_create_wrapper(import->display)));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(import->wrapper.get()), import->queue.get());
    import->registry.reset(wl_display_get_registry(import->wrapper.get()));
    wl_registry_add_listener(import->registry.get(), &Listener, import.get());
    wl_display_roundtrip_queue(import->display, import->queue.get());
    if (!import->importer)
        return;

    const QByteArray handle = parentWindow.mid(prefix.size()).toUtf8();
    import->imported.reset(zxdg_importer_v2_import_toplevel(import->importer.get(), handle.constData()));
    wl_display_flush(import->display);
    m_import = std::move(import);
}

ForeignParent::~ForeignParent()
{
    if (!m_import)
        return;
    wl_display* display = m_import->display;
    m_import.reset();
    wl_display_flush(display);
}

} // namespace ariadne
