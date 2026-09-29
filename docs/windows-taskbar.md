# Windows taskbar companion

The optional `search.exe` starts a taskbar companion on the primary
monitor, plus a non-interactive blue desktop background. It registers a Windows appbar and coexists with Explorer. It does not
change the configured shell, install autostart entries, or terminate Explorer.

## Build and launch

From Linux with the existing MinGW SDK:

```sh
./bw.sh -DEXPLORER_WINDOWS_SHELL=ON
```

The Windows release contains **one executable: `search.exe`**. Extract the ZIP
and run it directly to start the taskbar, background, launcher and tray client.
The Files button launches another process of that same executable in browser
mode. No sibling executable is needed, and closing a file window does not close
the taskbar. Native CMake output is named `searchwx.exe`; `bw.sh` copies it to
`build_windows/search.exe` for distribution.

```powershell
.\search.exe                         # Taskbar and background
.\search.exe --shell                 # Explicit companion mode
.\search.exe --browser               # Browser only
.\search.exe --browser "C:\Users"    # Browser at a folder
.\search.exe "C:\Users"              # Also opens a browser
.\search.exe --smoke-test            # Native integration check
```

`bw.sh` enables `EXPLORER_WINDOWS_SHELL` by default. Pass
`-DEXPLORER_WINDOWS_SHELL=OFF` for a browser-only Windows build. Direct CMake
builds retain the option's OFF default. Linux builds retain their browser-only
startup behavior and do not compile the Windows shell sources.

## Available controls

```text
+-----------------------------------------------------------------------+
| Start | Files | App window | App window | ... | All | Local time       |
+-----------------------------------------------------------------------+
             Windows' existing taskbar and notification area
```

- Window buttons follow visible, titled application windows, including minimized
  windows. Tool windows, cloaked windows and companion-owned windows are excluded.
  The list refreshes each second and preserves existing button order.
- Click a window to activate or restore it. Clicking the active window minimizes
  it. If Windows denies foreground activation, the target flashes instead.
- Right-click a task button for that window's system menu. Elevated applications
  may reject commands from a non-elevated companion.
- **All** lists every tracked window when the taskbar is crowded.
- **Files** launches this project's file browser.
- **Start > Programs** lists user and public Start-menu shortcuts, grouped into
  pages. The catalog refreshes on opening Start and is capped at 2,000 items.
- **Desktop background** in Start or the companion's context menu toggles a
  non-interactive blue gradient covering the primary monitor's work area. It
  starts enabled, has no icons or controls, and ignores clicks without taking
  keyboard focus. Hiding it exposes the original desktop; exiting destroys it.
  The existing Windows wallpaper setting is not changed.
- **Start > Desktop items** opens a windowed icon view of the user and public
  desktop folders. Double-click or press Enter to open the selected item.
- Start also provides Windows Settings, Task Manager and Exit companion.
- The clock opens Windows date/time settings.
- Ctrl+Alt+Space focuses the companion, then Tab/Shift+Tab and Space operate its
  native buttons. The shortcut is unavailable if another application owns it.
- The companion adds its own icon to Explorer's notification area. Double-click
  opens the browser; right-click offers desktop items and Exit companion.
- Closing the companion unregisters the appbar and removes its notification icon.
  A second instance exits without creating another panel.

## Implemented lifecycle behavior

The appbar reserves primary-monitor screen space through
[SHAppBarMessage](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars).
It repositions after display changes, lowers itself on fullscreen-app
notifications, and attempts to re-register the appbar and notification icon after
Explorer broadcasts `TaskbarCreated`. These paths require native Windows testing.

The notification icon uses the documented
[Shell notification-area client API](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar).
It does not implement a replacement host for other applications' tray icons.

## Validation

The Windows executable and test executables cross-compile with MinGW. Portable
window-filter and layout tests pass on Linux, along with the browser's filesystem
and GUI tests. No native Windows execution has been performed in this workspace.

Build the native smoke checks with:

```sh
./bw.sh -DEXPLORER_WINDOWS_SHELL=ON -DBUILD_TESTING=ON -DEXPLORER_GUI_TESTS=ON
```

On Windows, with no companion already running, execute:

```powershell
.\search.exe --smoke-test
```

The smoke mode briefly opens the appbar, background and desktop preview, then
launches the same executable in browser smoke mode and waits for a successful exit. It checks
control creation, background bounds, focus preservation, input suppression,
hide/show, own-window exclusion, reserved space and destruction/restoration on exit, then
returns 0 for success or 1 for failure. When built with native CMake, it is also
registered as `windows_shell_smoke` in CTest. Run in a disposable desktop session;
Explorer and the display layout must remain stable during this check.

Manual checks still required:

1. Launch several applications; switch, minimize, restore, close, and rename a
   window. Verify the title and active state update within one second.
2. Open enough windows for overflow; use All to activate a hidden button's window.
3. Use only the keyboard to open Start, launch an application and select a window.
4. Test 100%, 150% and 200% display scaling, a fullscreen application, and changes
   to primary-monitor resolution. Confirm maximized windows avoid the appbar.
5. Restart Explorer and verify panel reservation and notification-icon recovery.
6. Exit normally and verify maximized windows regain the reserved space.
7. Minimize all applications and verify the blue background is visible. Click and
   right-click it and confirm no activation, menu or selection occurs. Open a
   normal window, a dialog and a fullscreen application; each must remain above
   the background. Toggle Desktop background and verify the original desktop
   returns. Test Win+D, Explorer restart and display changes separately.
8. Test desktop shortcuts, user/public folders, inaccessible targets, Settings,
   and launching the browser from the same executable.

## Remaining scope

This is an initial companion implementation, not full Explorer parity. It has
one primary-monitor panel, system DPI scaling, title buttons and a shortcut-based
launcher. Per-monitor panels, dynamic DPI changes, task icons, pinning/grouping,
previews, jump lists, search and packaged-app enumeration are not implemented.
Launcher enumeration and desktop icon lookup are synchronous; redirected network
folders or slow icon handlers can delay those views.

The background is a companion-owned, non-activating window, not ownership of the
Windows desktop. Layering uses window enumeration and filters Explorer's Progman
and WorkerW host classes; those class names are compatibility heuristics, so
layering, Win+D and Explorer restart behavior need native verification. The
surface is repositioned on display/work-area changes and the one-second refresh.
It uses [non-activating window styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)
and [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos).
There is no wallpaper file picker, desktop drag-and-drop or persistent icon
positioning. The separate Desktop items command still opens an interactive preview
window; the background itself has no interactive content. Other
applications' tray icons, system flyouts and notification history remain provided
by Explorer. Replacement mode, session startup and crash recovery still require
the compatibility work in [the shell plan](windows-shell-plan.md).
