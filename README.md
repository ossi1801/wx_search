# Explorer

A native C++17 / wxWidgets file browser inspired by Windows XP Explorer, with a cleaner, resizable layout. Runs against your real filesystem; files open with the default desktop application.

## Features

- Back / Forward history, Up, Home, Refresh, editable address bar and clickable breadcrumbs.
- XP-inspired blue task pane with folder tasks, home locations and selected-item details.
- Details and large-icon views; click a details column to sort. Folders stay first.
- Recursive, case-insensitive filename search within the current folder. Press Enter or click the search icon to search; Escape or the clear button returns to the folder.
- Search runs on a background thread, skips inaccessible subdirectories and does not follow directory symlinks. Results are capped at 50,000 matches.
- Hidden dotfiles toggle, new folder, rename, copy path, properties and a right-click item menu.
- Native theme icons and controls, alternating detail rows, status bar and keyboard navigation.

## Build

Install a C++17 compiler, CMake 3.16+, and wxWidgets 3.2 development libraries (core and base).
On Debian/Ubuntu, the packages are `build-essential cmake libwxgtk3.2-dev`.

```sh
./b.sh
./build-host/searchwx
./build-host/searchwx /path/to/folder
```

`b.sh` uses Make and separate `build-host` / `build-flatpak` directories so cached SDK paths do not leak into host builds. Inside Flatpak, run `./build-flatpak/searchwx`. Set `BUILD_DIR` to override the output directory.

For a custom wxWidgets installation:

```sh
./b.sh -DwxWidgets_CONFIG_EXECUTABLE=/path/to/bin/wx-config
```

### Build a Windows .exe on Linux

On Debian/Ubuntu, install the cross-compiler once:

```sh
sudo apt install g++-mingw-w64-x86-64-posix make curl bzip2
./bw.sh
```

The result is **`build_windows/search.exe`**, a 64-bit Windows application. The first build downloads checksum-verified wxWidgets 3.2.8 sources and builds a static Windows SDK locally under `build_windows/`; later builds reuse it. No system wxWidgets installation for Windows is needed. The script uses four build jobs by default (`JOBS=2 ./bw.sh` uses two). From a Flatpak IDE it runs the build on the Linux host.

The executable statically links wxWidgets and the compiler runtime; Windows system DLLs are still required. Override `WX_CONFIG` to use an existing Windows wxWidgets SDK, or `CC` / `CXX` for alternate MinGW compilers. Native Windows execution must be tested separately.

## Shortcuts

| Action | Shortcut |
| --- | --- |
| Back / Forward | Alt+Left / Alt+Right |
| Parent folder / Home | Alt+Up / Alt+Home |
| Address / Search | Ctrl+L / Ctrl+F |
| Refresh | F5 |
| New folder | Ctrl+Shift+N |
| Rename | F2 |
| Copy path | Ctrl+Shift+C |
| Properties | Alt+Enter |
| Show hidden dotfiles | Ctrl+H |
| Leave search | Escape |

## Verification and scope

`./b.sh` also runs the filesystem tests. They cover shallow listing, recursive matching, hidden trees, metadata, cancellation, invalid paths, name validation and symlink cycles where supported.

This is a focused file browser, not a replacement for the Windows shell. Copy/move/delete, network discovery, thumbnails and shell extensions are not implemented. Hidden-file filtering currently follows dotfile naming, rather than the Windows hidden attribute. Shortcuts use standard English home subdirectories when they exist. Native widgets follow the host theme, so the appearance varies between operating systems.

Optional native GUI smoke checks (run in a desktop session):

```sh
cmake -S . -B build -DEXPLORER_GUI_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Linux the GUI test also needs GTK 3 development headers. It exercises navigation history, recursive search, hidden results, both views, resizing and superseding an active scan. It creates and cleans up its own temporary fixture. GTK builds save PNG layout captures in the build directory.

![Explorer details view](docs/explorer.png)
