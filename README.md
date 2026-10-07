# Ariadne

A file manager in the spirit of Nautilus, without the parts that keep getting removed.
Part of TDE.

Licensed under the GNU General Public License, version 3 or later; see [LICENSE](LICENSE).

## Installing

On Arch Linux, from the AUR: `tde-ariadne` for releases, `tde-ariadne-git` for the latest commit.
The PKGBUILDs live in [`packaging/arch`](packaging/arch).

## Building

Needs a C++23 compiler, CMake ≥ 3.28, Qt ≥ 6.8 (base), libwayland (with `wayland-scanner`) and
[libtde](https://github.com/zskamljic/libtde), installed or checked out next to this repository
(then it is built along).
At runtime, UDisks2 is used for mounting drives and unlocking encrypted ones, and gvfs (with
`gio` and its MTP and SMB backends) for phones, cameras and network shares, if they are installed.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

## Replacing Nautilus

```sh
ariadne --make-default
```

or **Make Default File Manager…** in the menu. This sets up, for your user:

- folders open in Ariadne (`inode/directory` in `~/.config/mimeapps.list`);
- Ariadne answers `org.freedesktop.FileManager1`, which applications call to show a folder or
  point at a file ("Show in Folder" in browsers, for example), through
  `~/.local/share/dbus-1/services`, which comes before the service Nautilus installs;
- Ariadne starts with your session (`~/.config/autostart/ariadne.desktop`) and keeps running
  without a window. Nautilus is started by the GNOME file chooser and file search and would
  take FileManager1 whenever it is free; this way it never is. New windows open at once, too.

Ariadne runs as a single instance: starting it again opens a window in the one running. After
an upgrade, the running one hands over to the new version once none of its windows are open.

## Picking files for other applications

Ariadne is the file chooser of xdg-desktop-portal in TDE: when an application opens or saves a
file through the portal, the window to pick it is Ariadne's, with the application's file types,
its own options, and a name to save under. GTK 4 applications and those in Flatpak use the
portal anyway; in TDE, Qt and GTK 3 applications are made to as well (`QT_QPA_PLATFORMTHEME`
and `GTK_USE_PORTAL`). Ariadne installs the portal (`tde.portal`) and starts when it is
needed; the TDE session's `tde-portals.conf` picks it for files. Through xdg-foreign, the
compositor learns which window the picker is for; Atlas places it over that window.

On GNOME, Nautilus cannot be removed, as `xdg-desktop-portal-gnome` depends on it; it stays
installed and unused. To go back, remove the two files above and run
`xdg-mime default org.gnome.Nautilus.desktop inode/directory`.

## Configuration

Configuration is Lua, in `~/.config/tde`. Changes apply to open windows as soon as a file
is saved.

- `config.lua` holds desktop-wide settings shared by all TDE applications: window button
  placement and order, theme (`arc-dark`, `arc` or `system`), corner radius, icon theme,
  colour overrides and the terminal. It is described with
  [libtde](https://github.com/zskamljic/libtde).
- `ariadne/config.lua` holds Ariadne's own defaults: view mode, sorting, hidden files,
  icon sizes, per-MIME-type icon overrides, and custom actions: your own commands in the
  context menu, for the file types you choose, optionally with a shortcut. See
  [`data/ariadne/config.lua`](data/ariadne/config.lua).
- `ariadne/state.lua` is written by Ariadne to remember changes made in its windows (zoom,
  hidden files, window and sidebar size, the default view). Editing `ariadne/config.lua`
  afterwards makes its settings win again.
- `ariadne/folders.lua` is written by Ariadne too: changing the view or sorting in a folder
  is remembered for that folder. "Use These Settings for All Folders" in the view menu makes
  them the default instead.

The desktop-wide config also names the terminal for "Open in Terminal" (`terminal = "kitty"`);
by default Ariadne uses `$TERMINAL` or the first of the usual terminals it finds. Terminals it
knows (GNOME Console, Ptyxis, GNOME Terminal, Ghostty, Konsole, kitty, Alacritty, foot, WezTerm,
Xfce Terminal) are told the folder explicitly; others start in it as their working directory.

Thumbnails follow the freedesktop.org thumbnail spec and share `~/.cache/thumbnails` with
other file managers. Images are scaled by Ariadne itself; other files (PDFs, videos, …) use
the thumbnailers installed in `/usr/share/thumbnailers`.

Archives (zip, tarballs, 7z, rar, ISO images and whatever else libarchive reads) open like
folders, read-only: open or copy what is in them, or extract all or part of them. "Open With"
still offers the archive manager, and `archives_as_folders = false` in the config makes that
the default again.

The sidebar shows how full each drive is; Properties shows a folder's free space, and changes
permissions for one item or several, or for everything inside a folder.

Bookmarks are shared with GTK file choosers through `~/.config/gtk-3.0/bookmarks`.
Drag a folder onto the sidebar to bookmark it; drag bookmarks to reorder them.

## Keys

| Key | Action |
| --- | --- |
| Alt+Left / Alt+Right, mouse back/forward | Back / forward |
| Alt+Up | Parent folder |
| Alt+Home | Home |
| Ctrl+L, or click empty space in the path bar | Type a location; folder names complete as you type, Tab accepts |
| Ctrl+1 / Ctrl+2 | Grid / list view |
| Ctrl+H | Show hidden files |
| Ctrl+Plus / Ctrl+Minus / Ctrl+0, Ctrl+wheel | Zoom |
| Ctrl+Shift+N | New folder (right-click empty space for new documents, including ~/Templates) |
| Delete | Move to Trash (in Trash: delete permanently, after asking) |
| Shift+Delete | Delete permanently, after asking |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste files (interoperates with GTK and KDE apps) |
| Ctrl+Z | Undo the last trash, move, copy, rename or new item |
| F2 | Rename in place; with several items selected, rename them with find and replace |
| Ctrl+I | Properties |
| Ctrl+D | Bookmark the current folder |
| F5, Ctrl+R | Reload |
| Ctrl+N / Ctrl+W / Ctrl+Q | New window / close window / quit |
| Middle click | Open a folder in a new window |
| Drag and drop | Move within a drive, copy across drives; hold Ctrl to copy, Shift to move |
| Typing, Ctrl+F | Search this folder and everything below it (Esc goes back) |
