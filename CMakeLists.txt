cmake_minimum_required(VERSION 3.28)

project(ariadne
    VERSION 0.1.0
    DESCRIPTION "A file manager"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ARIADNE_BUILD_TESTS "Build the unit tests" ON)
option(ARIADNE_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent)
find_package(PkgConfig REQUIRED)
pkg_search_module(LUA REQUIRED IMPORTED_TARGET lua lua5.5 lua-5.5 lua5.4 lua-5.4 lua54)

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
    src/core/DefaultFileManager.cpp
    src/core/DeviceMonitor.cpp
    src/core/DirectoryListing.cpp
    src/core/FileOperations.cpp
    src/core/Gvfs.cpp
    src/core/Location.cpp
    src/core/Search.cpp
    src/core/Terminal.cpp
    src/core/Thumbnailer.cpp
    src/core/Transfer.cpp
    src/tde/DesktopConfig.cpp
    src/tde/LuaConfig.cpp
)
target_include_directories(ariadne-core PUBLIC src)
target_link_libraries(ariadne-core
    PUBLIC Qt6::Core Qt6::Gui Qt6::DBus Qt6::Concurrent
    PRIVATE PkgConfig::LUA)

add_library(ariadne-ui STATIC
    src/ui/Application.cpp
    src/ui/BatchRenameDialog.cpp
    src/ui/Dialog.cpp
    src/ui/DirectoryModel.cpp
    src/ui/DragDrop.cpp
    src/ui/FileSortProxy.cpp
    src/ui/FileManagerService.cpp
    src/ui/FileView.cpp
    src/ui/FramelessHelper.cpp
    src/ui/GridView.cpp
    src/ui/HeaderBar.cpp
    src/ui/Jobs.cpp
    src/ui/JobsButton.cpp
    src/ui/MainWindow.cpp
    src/ui/OpenWithDialog.cpp
    src/ui/PathBar.cpp
    src/ui/PropertiesDialog.cpp
    src/ui/Sidebar.cpp
    src/ui/Theme.cpp
    src/ui/Toast.cpp
    src/ui/WindowButtons.cpp
)
target_link_libraries(ariadne-ui PUBLIC ariadne-core Qt6::Widgets Qt6::Concurrent)

qt_add_executable(ariadne src/main.cpp)
target_link_libraries(ariadne PRIVATE ariadne-ui)
target_compile_definitions(ariadne PRIVATE ARIADNE_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS ariadne RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/ariadne.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
foreach(service io.github.zskamljic.Ariadne org.freedesktop.FileManager1)
    configure_file(data/dbus/${service}.service.in ${service}.service @ONLY)
endforeach()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.zskamljic.Ariadne.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.FileManager1.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/ariadne)
# Example configs, laid out like ~/.config/tde. The desktop-wide one belongs to all of TDE,
# so it is kept out of shared locations that a future common package would own.
install(FILES data/tde/config.lua DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/tde)
install(FILES data/ariadne/config.lua DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/tde/ariadne)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(ARIADNE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
