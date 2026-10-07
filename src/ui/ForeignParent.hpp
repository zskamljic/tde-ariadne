#pragma once

#include <QString>

#include <memory>

namespace ariadne {

// The window of another program that one of Ariadne's is shown for, named as the portals name
// it: "wayland:" and the handle that program exported it under. It is imported through
// xdg-foreign for as long as this lives, which tells the compositor that the window Ariadne
// shows next belongs with it. Elsewhere than on Wayland, or without xdg-foreign, nothing is done.
class ForeignParent {
public:
    explicit ForeignParent(const QString& parentWindow);
    ~ForeignParent();

    ForeignParent(const ForeignParent&) = delete;
    ForeignParent& operator=(const ForeignParent&) = delete;

private:
    struct Import;
    std::unique_ptr<Import> m_import;
};

} // namespace ariadne
