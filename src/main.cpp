#include "core/Config.hpp"
#include "core/DefaultFileManager.hpp"
#include "core/Location.hpp"
#include "tde/DesktopConfig.hpp"
#include "tde/LuaConfig.hpp"
#include "ui/Application.hpp"
#include "ui/FileManagerService.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QProcess>
#include <QTimer>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    // Every widget should draw into its window's single buffer. Native child windows are
    // separate opaque surfaces that cover the transparent rounded window corners; should a
    // widget ever need one, its siblings must not get one too.
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    // Qt registers its app ID with xdg-desktop-portal, which fails harmlessly when the desktop
    // already did so on launch, but warns on every start. QT_LOGGING_RULES still overrides this.
    QLoggingCategory::setFilterRules(u"qt.qpa.services.warning=false"_s);
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"ariadne"_s);
    QApplication::setApplicationVersion(QStringLiteral(ARIADNE_VERSION));
    QApplication::setDesktopFileName(u"ariadne"_s);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"A file manager."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption configOption(u"config-dir"_s,
        u"Read the TDE config from <dir> instead of ~/.config/tde."_s, u"dir"_s, tde::configDirectory());
    parser.addOption(configOption);
    const QCommandLineOption serviceOption(u"service"_s, u"Start without a window, to answer D-Bus calls."_s);
    parser.addOption(serviceOption);
    const QCommandLineOption backgroundOption(
        u"background"_s, u"Keep running without a window, ready to show folders at once."_s);
    parser.addOption(backgroundOption);
    const QCommandLineOption makeDefaultOption(
        u"make-default"_s, u"Make Ariadne the file manager of your desktop, then exit."_s);
    parser.addOption(makeDefaultOption);
    parser.addPositionalArgument(
        u"locations"_s, u"Folders to open, or files to show in their folder."_s, u"[locations...]"_s);
    parser.process(app);

    // Kept, as the file is replaced when the package is upgraded.
    const QString executable = QCoreApplication::applicationFilePath();

    if (parser.isSet(makeDefaultOption)) {
        if (const auto made = ariadne::defaults::makeDefault(executable); !made) {
            std::println(stderr, "ariadne: {}", made.error().toStdString());
            return 1;
        }
        if (!ariadne::FileManagerService::isRunning())
            QProcess::startDetached(executable, {u"--background"_s});
        ariadne::FileManagerService::takeOverFromNautilus();
        std::println("Ariadne is now your file manager: it opens folders, shows files for other "
                     "applications, and starts with your session.");
        return 0;
    }

    // Folders to open, and files to show selected in theirs.
    const QUrl workingDirectory = ariadne::location::fromLocalPath(QDir::currentPath());
    QList<QUrl> folders;
    QList<QUrl> items;
    for (const QString& argument : parser.positionalArguments()) {
        const auto url = ariadne::location::fromUserInput(argument, workingDirectory);
        if (!url)
            continue;
        const QFileInfo info(ariadne::location::localPath(*url));
        (info.exists() && !info.isDir() ? items : folders) << *url;
    }
    const bool background = parser.isSet(backgroundOption);
    const bool service = parser.isSet(serviceOption) || background;
    if (!service && folders.isEmpty() && items.isEmpty())
        folders << ariadne::location::home();

    // One Ariadne at a time, unless started with a config of its own (as when trying one out).
    const bool shared = !parser.isSet(configOption);
    const auto toStrings = [](const QList<QUrl>& urls) {
        QStringList strings;
        for (const QUrl& url : urls)
            strings << url.toString();
        return strings;
    };
    if (shared && !service && ariadne::FileManagerService::forwardToRunning(toStrings(folders), toStrings(items)))
        return 0;

    const QString configDir = parser.value(configOption);
    tde::setDesktop(tde::loadDesktopConfig(configDir + u"/config.lua"_s));
    ariadne::theme::apply(app, tde::desktop().appearance);
    QApplication::setWindowIcon(ariadne::theme::themeIcon({u"system-file-manager"_s, u"folder"_s}));

    const QString stateFile = configDir + u"/ariadne/state.lua"_s;
    ariadne::Application ariadne(ariadne::loadConfig(configDir + u"/ariadne/config.lua"_s, stateFile), stateFile);

    ariadne::FileManagerService fileManager(ariadne);
    if (shared && !fileManager.registerOnBus()) {
        // Another one started meanwhile.
        if (service || ariadne::FileManagerService::forwardToRunning(toStrings(folders), toStrings(items)))
            return 0;
    }

    for (const QUrl& folder : std::as_const(folders))
        ariadne.openWindow(folder);
    for (const QUrl& item : std::as_const(items)) {
        const QString name = QFileInfo(ariadne::location::localPath(item)).fileName();
        ariadne.openWindow(ariadne::location::parent(item).value_or(item), name);
    }

    // Kept running for the session; after an upgrade, the new version takes over once no
    // window is open.
    QFileSystemWatcher binaryWatcher;
    bool restartPending = false;
    const auto restartIfIdle = [&] {
        if (!restartPending || ariadne.hasWindows())
            return;
        fileManager.unregisterFromBus();
        QProcess::startDetached(executable, {u"--background"_s});
        QApplication::quit();
    };
    if (background) {
        QApplication::setQuitOnLastWindowClosed(false);
        binaryWatcher.addPath(executable);
        QObject::connect(&binaryWatcher, &QFileSystemWatcher::fileChanged, &app, [&] {
            restartPending = true;
            QTimer::singleShot(3000, &app, restartIfIdle); // once the package manager is done
        });
        // Closed windows go away just after this.
        QObject::connect(
            &app, &QGuiApplication::lastWindowClosed, &app, [&] { QTimer::singleShot(500, &app, restartIfIdle); });
    } else if (service) {
        // Started by D-Bus: the call that started it opens a window. Should none come, do not linger.
        QTimer::singleShot(30'000, &app, [&ariadne] {
            if (!ariadne.hasWindows())
                QApplication::quit();
        });
    }

    return QApplication::exec();
}
