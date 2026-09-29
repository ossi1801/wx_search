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
