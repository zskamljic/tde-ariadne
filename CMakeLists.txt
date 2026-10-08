cmake_minimum_required(VERSION 3.28)

project(ariadne
    VERSION 0.3.0
    DESCRIPTION "A file manager"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ARIADNE_BUILD_TESTS "Build the unit tests" ON)
option(ARIADNE_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent)
find_package(PkgConfig REQUIRED)
pkg_search_module(ARCHIVE REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER)
    message(FATAL_ERROR "wayland-scanner was not found")
endif()
# libtde as installed; for working on both, a checkout next to this one is used instead.
find_package(Tde 0.2 QUIET)
if(NOT Tde_FOUND)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../libtde/CMakeLists.txt)
        message(STATUS "Using libtde from ${CMAKE_CURRENT_SOURCE_DIR}/../libtde")
        add_subdirectory(../libtde ${CMAKE_CURRENT_BINARY_DIR}/libtde EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "libtde was not found: install it, or check it out next to Ariadne.")
    endif()
endif()

qt_standard_project_setup()
# Only system libraries are linked, so installed binaries need no RPATH.
set(CMAKE_SKIP_INSTALL_RPATH ON)

add_compile_options(-Wall -Wextra -Wpedantic)
if(ARIADNE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(ariadne-core STATIC
    src/core/Applications.cpp
    src/core/Archives.cpp
    src/core/BatchRename.cpp
    src/core/Bookmarks.cpp
    src/core/ClipboardFormat.cpp
    src/core/Config.cpp
    src/core/CustomActions.cpp
    src/core/DefaultFileManager.cpp
    src/core/DeviceMonitor.cpp
    src/core/DirectoryListing.cpp
    src/core/FileChooser.cpp
    src/core/FileOperations.cpp
    src/core/Gvfs.cpp
    src/core/Location.cpp
    src/core/Search.cpp
    src/core/Terminal.cpp
    src/core/Thumbnailer.cpp
    src/core/Transfer.cpp
)
target_include_directories(ariadne-core PUBLIC src)
target_link_libraries(ariadne-core
    PUBLIC Qt6::Core Qt6::Gui Qt6::DBus Qt6::Concurrent Tde::Tde
    PRIVATE PkgConfig::ARCHIVE)

# xdg-foreign, which places a file picker with the window of the program it is for.
set(foreign_xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-foreign-unstable-v2.xml)
set(foreign_header ${CMAKE_CURRENT_BINARY_DIR}/xdg-foreign-unstable-v2-client-protocol.h)
set(foreign_code ${CMAKE_CURRENT_BINARY_DIR}/xdg-foreign-unstable-v2-protocol.c)
add_custom_command(OUTPUT ${foreign_header}
    COMMAND ${WAYLAND_SCANNER} client-header ${foreign_xml} ${foreign_header} DEPENDS ${foreign_xml})
add_custom_command(OUTPUT ${foreign_code}
    COMMAND ${WAYLAND_SCANNER} private-code ${foreign_xml} ${foreign_code} DEPENDS ${foreign_xml})

add_library(ariadne-ui STATIC
    src/ui/Application.cpp
    src/ui/BatchRenameDialog.cpp
    src/ui/Dialogs.cpp
    src/ui/DirectoryModel.cpp
    src/ui/DragDrop.cpp
    src/ui/FileChooserPortal.cpp
    src/ui/FileManagerService.cpp
    src/ui/FilePicker.cpp
    src/ui/FileSortProxy.cpp
    src/ui/FileView.cpp
    src/ui/ForeignParent.cpp
    src/ui/GridView.cpp
    src/ui/Jobs.cpp
    src/ui/JobsButton.cpp
    src/ui/MainWindow.cpp
    src/ui/OpenWithDialog.cpp
    src/ui/PathBar.cpp
    src/ui/PropertiesDialog.cpp
    src/ui/Sidebar.cpp
    src/ui/StyleSheet.cpp
    ${foreign_header}
    ${foreign_code}
)
target_include_directories(ariadne-ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ariadne-ui PUBLIC ariadne-core Qt6::Widgets Qt6::Concurrent PRIVATE PkgConfig::WAYLAND_CLIENT)

qt_add_executable(ariadne src/main.cpp)
target_link_libraries(ariadne PRIVATE ariadne-ui)
target_compile_definitions(ariadne PRIVATE ARIADNE_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS ariadne RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/ariadne.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
foreach(service io.github.zskamljic.Ariadne org.freedesktop.FileManager1 org.freedesktop.impl.portal.desktop.tde)
    configure_file(data/dbus/${service}.service.in ${service}.service @ONLY)
endforeach()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.zskamljic.Ariadne.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.FileManager1.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/ariadne)
# The file chooser of xdg-desktop-portal for TDE.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.tde.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES data/portal/tde.portal DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
# The example config, laid out like ~/.config/tde. The desktop-wide one comes with libtde.
install(FILES data/ariadne/config.lua DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/tde/ariadne)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(ARIADNE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
