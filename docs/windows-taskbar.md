# Windows taskbar companion

The optional `search.exe` starts a taskbar companion on the primary
monitor, plus a non-interactive desktop background. Before creating the taskbar,
it force-terminates `explorer.exe` processes in the current Windows session,
waits for exit, retries startup races and checks that native taskbars are gone
on all monitors. Direct termination falls back to the system `taskkill /F /PID`.
The companion's own process is excluded even if its executable is renamed.

Startup also force-stops the current-session applications from the supplied
cleanup batch: Search, Start menu, Widgets, Phone Link, Game Bar, Teams, Skype,
Edge WebView/update hosts and the listed inbox apps (Calculator, Photos, Mail,
Music, Video, Recorder, Weather, News, Store, Feedback, Maps, Camera, Alarms,
Notes, Snipping Tool and Paint). This can close open applications and unsaved
work. TextInputHost is targeted only when it owns a window titled Copilot,
Clipboard History or Emoji Panel, matching the script's prefix filters.
Unavailable optional processes are ignored; failure to stop Explorer aborts
startup. Defender and other services are not stopped, and no CMD window is
launched. Cleanup runs once at startup. This also closes Windows File Explorer
windows. Startup stops with an error if Explorer cannot be terminated. Browser-only
launches do not stop Explorer, and a second taskbar instance exits before this step.
It does not change the configured shell or install autostart entries.

The taskbar clears Explorer's old screen reservation and aligns with the bottom
of the primary monitor. When Explorer's appbar service is unavailable, it reserves
the work area directly and releases that space on exit. Explorer is not restarted
automatically; exit the companion, then run `explorer.exe` from Task Manager's
Run new task to restore it.

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
| Windows icon | Folder icon | App window | App window | ... | Window grid | Network icon | Speaker icon | Time / date       |
+-----------------------------------------------------------------------+
```

- Window buttons follow visible, titled application windows, including minimized
  windows. Tool windows, cloaked windows and companion-owned windows are excluded.
  The list refreshes each second and preserves existing button order.
  Buttons show each window's application icon (window icon, class icon, then
  executable icon), scaled with system DPI. The title appears in a tooltip and
  remains the accessible name. If no icon is available, the button shows text.
  Icon requests use a timeout so unresponsive applications cannot block indefinitely.
- Click a window to activate or restore it. Clicking the active window minimizes
  it. If Windows denies foreground activation, the target flashes instead.
- Right-click a task button for that window's system menu. Elevated applications
  may reject commands from a non-elevated companion.
- **All** lists every tracked window when the taskbar is crowded.
- The **Start** taskbar button shows a blue Windows-style icon; **Files** shows
  the standard Windows folder icon. The **All windows** button shows an outlined window-grid icon. All three scale
  with system DPI, keep accessible names and show their labels as tooltips.
  The clock shows local 24-hour `HH:mm` time above the date in `DD.MM.YYYY` format.
- The **Network** and **Sound** buttons show DPI-scaled network and speaker icons,
  with accessible names and tooltips.
- **Network** beside Sound opens Windows network settings. Right-click it for
  Wi-Fi settings, Ethernet settings or the classic network adapter panel.
- **Sound** beside the clock opens the Windows volume mixer to adjust volume and mute.
  Right-click it for Sound settings or the classic playback/recording device panel.
- **Files** launches this project's file browser.
- **Start > Applications** lists user and public Start-menu shortcuts, grouped by
  initial letter (with numbers and symbols under #). Each entry shows its shortcut's application icon, with a generic icon
  when extraction fails. Icons scale with the system DPI, load when their submenu
  opens, and remain cached until the shortcut changes or disappears. The catalog
  refreshes on opening Start and is capped at 2,000 items.
- **Start > System** contains Windows Settings and Task Manager.
- **Start > Power** offers Shut down and Restart, each with a confirmation
  defaulting to Cancel/No. Windows handles the operation without forced app closure.
- **Start > Desktop > Background folder** opens `%LOCALAPPDATA%\Rexplorer\Backgrounds`
  in this application's file browser. The folder is created automatically. Put a
  PNG, JPG/JPEG or BMP image directly in it; subfolders are ignored. With multiple
  images, the first filename in ordinal alphabetical order is selected (extension
  matching is case-insensitive). Use a single image to make the choice explicit.
- **Desktop > Show background** in Start or the companion's context menu toggles the
  background covering the primary monitor's work area, excluding the taskbar.
  The selected image is stretched to fill that area, including aspect-ratio
  distortion, and resizes after resolution/work-area changes. Missing, unreadable
  or invalid images show the original blue gradient. Decoded images are limited
  to 256 MiB; larger images also use the blue placeholder.
  The visible background checks the folder once per second and reloads when the
  selected filename, file size or modification time changes. Adding, replacing or
  removing an image requires no restart. A hidden background updates when shown.
  It starts enabled with desktop icons. Empty wallpaper ignores clicks without
  taking keyboard focus; icons accept mouse and keyboard input. Hiding it also
  hides the icons; exiting destroys the surface.
  The existing Windows wallpaper setting is not changed.
- Icons from the user and public desktop folders appear directly on the primary
  desktop at startup, beneath application windows and above the wallpaper.
  Double-click or press Enter to open the selected item. **Start > Refresh desktop
  icons** reloads the folders without opening a separate window.
- Start also provides Windows Settings, Task Manager and Exit companion.
- The clock opens Windows date/time settings.
- Ctrl+Alt+Space focuses the companion, then Tab/Shift+Tab and Space operate its
  native buttons. The shortcut is unavailable if another application owns it.
- If Explorer restarts and broadcasts `TaskbarCreated`, the companion stops it
  again and reclaims its taskbar space. Exit the companion before restoring
  Windows Explorer through Task Manager.
- Closing the companion unregisters the appbar and removes its notification icon.
  A second instance exits without creating another panel.

## Implemented lifecycle behavior

When available, the appbar reserves primary-monitor screen space through
[SHAppBarMessage](https://learn.microsoft.com/en-us/windows/win32/shell/application-desktop-toolbars).
It repositions after display changes, lowers itself on fullscreen-app
notifications, and stops Explorer again before re-registering its bar after
Explorer broadcasts `TaskbarCreated`. These paths require native Windows testing.

The notification icon uses the documented
[Shell notification-area client API](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar).
It does not implement a replacement host for other applications' tray icons.

## Validation

The Windows executable and test executables cross-compile with MinGW. Portable
window-filter, layout, background-file selection/reload and filesystem tests run
on Linux. The GUI smoke check requires a display. No native Windows execution
has been performed in this workspace.

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
hide/show, own-window exclusion, reserved space, task-icon attachment/text fallback, menu-icon creation/scaling/cache
reuse and destruction/restoration on exit, then
returns 0 for success or 1 for failure. When built with native CMake, it is also
registered as `windows_shell_smoke` in CTest. Run in a disposable desktop session;
The check stops Explorer just like normal taskbar startup. Keep the display layout stable.

Manual checks still required:

1. Launch several applications; switch, minimize, restore, close, and rename a
   window. Verify the title and active state update within one second.
2. Open enough windows for overflow; use All to activate a hidden button's window.
3. Use only the keyboard to open Start, launch an application and select a window.
4. Test 100%, 150% and 200% display scaling, a fullscreen application, and changes
   to primary-monitor resolution. Confirm maximized windows avoid the appbar.
5. Restart Explorer while the companion runs; verify the Windows taskbar disappears
   again, including on secondary monitors, and the companion reserves its space.
6. Exit normally and verify maximized windows regain the reserved space.
7. Minimize all applications and verify the blue background is visible. Click and
   right-click it and confirm no activation, menu or selection occurs. Open a
   normal window, a dialog and a fullscreen application; each must remain above
   the background. Toggle Desktop background and verify the original desktop
   returns. Test Win+D, Explorer restart and display changes separately.
8. Test desktop shortcuts, user/public folders, inaccessible targets, Settings,
   and launching the browser from the same executable.
9. Open Programs submenus and confirm common applications show their own icons
   at 100%, 150% and 200% scaling. Check a broken shortcut's fallback, change a
   shortcut icon, then reopen Start. Repeat menu opening to check icon reuse.
10. Use Open background folder, add PNG/JPEG/BMP images, and confirm stretching
    ends at the taskbar edge. Replace the selected file, remove it, and add a
    corrupt image. Verify updates and the blue fallback. Check multiple files,
    resolution changes, hide/show and focus preservation.

## Remaining scope

This is an initial companion implementation, not full Explorer parity. It has
one primary-monitor panel, system DPI scaling, application icon buttons and a shortcut-based
launcher. Per-monitor panels, dynamic DPI changes, pinning/grouping,
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
Background selection uses the background folder; there is no wallpaper file
picker, desktop drag-and-drop or persistent icon positioning. Desktop icons
auto-arrange in columns on the background surface. Other
applications' tray icons, system flyouts and notification history are unavailable
while Explorer is stopped. Session startup and crash recovery still require
the compatibility work in [the shell plan](windows-shell-plan.md).

## Spotlight search

Press **Alt+Space** while the companion is running to toggle Search. The dark,
DPI-scaled overlay appears on the pointer's monitor. Type to search, use Up/Down
to select a result and Enter to open it; clicking a row also opens it. Escape or
switching to another window dismisses the overlay. An empty query lists apps.

Search indexes shortcuts in both Start Menu Programs folders and files in
Desktop, Documents, Downloads, Pictures, Music and Videos. Matching ignores case
and supports multiple words across filenames and paths. Exact names and prefixes
rank above substring and folder matches; apps win otherwise equivalent matches.
Scanning and ranking run on background threads, typing is debounced by 45 ms,
and only the best eight results are sent to the UI. Inaccessible folders are
skipped, directory symlinks are not followed, and scanning is bounded to 50,000
entries per root and twelve directory levels. The index is held in memory for
the companion session; restart the companion to include newly created files.
This is not a whole-drive or file-content search. If another application has
registered Alt+Space, startup reports the conflict. Ctrl+Alt+Space still focuses
the taskbar Start button.
