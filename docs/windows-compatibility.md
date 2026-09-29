# Windows compatibility and validation tracking

## Verified in this workspace

- Linux: `./b.sh -DEXPLORER_GUI_TESTS=ON` built the browser and passed both
  filesystem and GUI smoke tests in the host desktop session.
- Windows: `./bw.sh` built `build_windows/search.exe` through CMake with the
  existing MinGW x64 / static wxWidgets SDK.
- No native Windows execution has been performed. Cross-build success does not
  establish runtime or shell compatibility.

## Native browser baseline

Run on a disposable Windows VM before introducing native behavior changes. Record
Windows edition/build, architecture, display scaling and account type with results.

- Launch `search.exe`, navigate using address/history/breadcrumbs, and open files.
- Verify Unicode paths, access-denied directories, unavailable shares and links.
- Exercise copy, cut, paste, rename, new file and confirmed permanent deletion
  using disposable fixtures only. The current browser does not use Recycle Bin.
- Build with `./bw.sh -DBUILD_TESTING=ON -DEXPLORER_GUI_TESTS=ON`, copy the test
  executables from `build_windows/cmake` to Windows, and run them there.
- Confirm GUI smoke exit status and that fixture cleanup succeeds.
- Record startup time and idle memory before native integrations are added.

## Shell feasibility gates (all pending)

| Gate | Required experiment | Current status |
| --- | --- | --- |
| Target versions | Select maintained Windows builds and editions available for VM testing | Not selected |
| Tray hosting | Third-party add/update/remove, callbacks, popup menus, shell restart | Not implemented or tested |
| Work area | Companion appbar versus Explorer-absent per-monitor work areas | Not implemented or tested |
| Application activation | Desktop apps, packaged apps, associations, Settings | Not tested without Explorer |
| Notifications | Toast activation and history with Explorer absent | Not tested |
| Session startup | Startup entries execute once; session-end handling succeeds | Not implemented or tested |
| Recovery | Kill child and host; bound restart loops; restore configuration and Explorer | Not implemented or tested |
| Deployment | Supported edition activation, dedicated recovery account, uninstall | Not implemented or tested |

Replacement mode must remain unavailable until mandatory compatibility and
recovery gates pass. Browser development can continue independently. A future
companion preview must not silently change the user's configured shell.

## Taskbar companion implementation

The optional companion is now implemented and cross-compiles. Its portable
window-filter and DPI/overflow layout tests pass on Linux. This adds a primary
monitor appbar, running-window controls, a Start-menu shortcut launcher, a
windowed desktop icon preview and a notification-area client icon. It does not
host third-party notification icons or replace the desktop shell.

Native runtime checks remain pending, including the new `--smoke-test` lifecycle
check. Follow [the taskbar validation procedure](windows-taskbar.md) before
marking any native compatibility gate passed. Explorer restart registration is
implemented but untested; automatic restart after a companion crash is not.

## Non-interactive background

The companion now has a primary-monitor blue gradient background, visible by
default and toggled from its menu. Its code uses a non-activating tool window,
ignores clicks and context menus, follows the work area and destroys the surface
on exit. The native smoke check now covers focus preservation, bounds, input
suppression, toggling, task-list exclusion and destruction. These native checks
are compiled but have not been executed here. Explorer host layering and Win+D
remain manual checks; existing wallpaper settings are not modified.

## Integrated executable and release 1.2.0

One Windows `search.exe` contains the browser, taskbar, background, shortcut
launcher and tray client. Files launches the same executable with `--browser`.
The native shell smoke check also launches `--browser-smoke-test`, waits for a
successful browser process exit and checks that the shell stays alive. That
native check is cross-compiled but has not been executed in this workspace.
Versioned ZIPs now have a repeatable packager that verifies archived binary bytes
and regenerates checksums. Linux still starts directly in browser mode.
