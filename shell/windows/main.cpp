#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <cwctype>
#include <map>
#include <set>
#include "taskbar_model.h"
#include "background.h"

namespace fs = std::filesystem;
namespace {
constexpr UINT AppbarMessage = WM_APP + 1, TrayMessage = WM_APP + 2;
constexpr int Start = 100, Files = 101, Desktop = 102, More = 103, Exit = 104, Clock = 107, Background = 108, BackgroundFolder = 109;
constexpr int AppFirst = 1000, WindowFirst = 10000;
struct Item { std::wstring label; fs::path path; };
struct Task { HWND window; std::wstring title; };
HWND bar{}, desktop{}, desktopList{};
std::vector<Task> tasks;
std::vector<Item> apps, desktopItems;
bool registered = false, positioning = false, fullscreen = false;
std::vector<HWND> taskButtons;
HWND startButton{}, filesButton{}, moreButton{}, clockButton{};
void layoutButtons();
UINT taskbarCreated{};
HFONT font{};
int height = 42, buttonWidth = 170, visibleTasks = 0;
unsigned dpi = 96;
HWND priorForeground{};
bool manualWorkArea = false;
RECT savedWorkArea{};
struct MenuIcon { HBITMAP bitmap{}; fs::file_time_type modified{}; };
std::map<fs::path, MenuIcon> menuIcons;

void clearMenuIcons() {
    for (const auto& entry : menuIcons) if (entry.second.bitmap) DeleteObject(entry.second.bitmap);
    menuIcons.clear();
}

HBITMAP applicationIcon(const fs::path& path) {
    std::error_code ec;
    const auto modified = fs::last_write_time(path, ec);
    auto& cached = menuIcons[path];
    if (cached.bitmap && cached.modified == modified) return cached.bitmap;
    if (cached.bitmap) DeleteObject(cached.bitmap);
    cached = {nullptr, modified};
    SHFILEINFOW info{};
    SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON);
    HICON icon = info.hIcon;
    if (!icon) icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    if (!icon) return nullptr;
    const int size = MulDiv(16, dpi, 96);
    BITMAPINFO dib{}; dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = size; dib.bmiHeader.biHeight = -size;
    dib.bmiHeader.biPlanes = 1; dib.bmiHeader.biBitCount = 32; dib.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &dib, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (dc && bitmap) {
        const auto old = SelectObject(dc, bitmap);
        std::fill(pixels, pixels + size * size, 0);
        DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        GdiFlush();
        // Legacy icons have an AND mask instead of alpha. Convert that mask
        // so native menus can composite both old and modern application icons.
        const bool hasAlpha = std::any_of(pixels, pixels + size * size, [](DWORD pixel) { return (pixel >> 24) != 0; });
        if (!hasAlpha) {
            DWORD* maskPixels = nullptr;
            HBITMAP mask = CreateDIBSection(dc, &dib, DIB_RGB_COLORS, reinterpret_cast<void**>(&maskPixels), nullptr, 0);
            if (mask) {
                SelectObject(dc, mask);
                std::fill(maskPixels, maskPixels + size * size, 0x00ffffff);
                DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_MASK);
                GdiFlush();
                for (int i = 0; i < size * size; ++i)
                    pixels[i] = (maskPixels[i] & 0x00ffffff) ? 0 : (pixels[i] | 0xff000000);
                SelectObject(dc, bitmap);
                DeleteObject(mask);
            }
        }
        SelectObject(dc, old);
        cached.bitmap = bitmap;
    } else if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    DestroyIcon(icon);
    return cached.bitmap;
}

void populateMenuIcons(HMENU popup) {
    for (int i = 0; i < GetMenuItemCount(popup); ++i) {
        const UINT id = GetMenuItemID(popup, i);
        if (id < AppFirst || id >= AppFirst + apps.size()) continue;
        MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_BITMAP;
        item.hbmpItem = applicationIcon(apps[id - AppFirst].path);
        SetMenuItemInfoW(popup, i, TRUE, &item);
    }
}

// Match the supplied cleanup script, scoped to this desktop session. Never
// target our own PID, even if the distributed executable is renamed Explorer.
const wchar_t* cleanupImages[] = {
    L"explorer.exe", L"SearchHost.exe", L"calc.exe", L"Photos.exe",
    L"HxOutlook.exe", L"Music.UI.exe", L"Video.UI.exe", L"SoundRecorder.exe",
    L"bingweather.exe", L"bingnews.exe", L"WinStore.App.exe", L"GameBar.exe",
    L"FeedbackHub.exe", L"Maps.exe", L"Microsoft.WindowsCamera.exe",
    L"Microsoft.WindowsAlarms.exe", L"Microsoft.Notes.exe", L"SnippingTool.exe",
    L"Microsoft.Windows.Photos.exe", L"mspaint.exe", L"StartMenuExperienceHost.exe",
    L"Widgets.exe", L"WidgetService.exe", L"PhoneExperienceHost.exe",
    L"GameBarFT.exe", L"ms-teams.exe", L"msedgewebview2.exe", L"Skype.exe",
    L"SkypeApp.exe", L"SkypeBackgroundHost.exe", L"MicrosoftEdgeUpdate.exe",
    L"MicrosoftEdgeSH.exe"
};

BOOL CALLBACK cleanupInputWindow(HWND window, LPARAM data) {
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    if (wcsncmp(title, L"Copilot", 7) == 0 ||
        wcsncmp(title, L"Clipboard History", 17) == 0 ||
        wcsncmp(title, L"Emoji Panel", 11) == 0) {
        DWORD pid{}; GetWindowThreadProcessId(window, &pid);
        reinterpret_cast<std::set<DWORD>*>(data)->insert(pid);
    }
    return TRUE;
}

bool forceStop(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (process) {
        const bool stopped = WaitForSingleObject(process, 0) == WAIT_OBJECT_0 ||
            (TerminateProcess(process, 0) && WaitForSingleObject(process, 2000) == WAIT_OBJECT_0);
        CloseHandle(process);
        if (stopped) return true;
    } else if (GetLastError() == ERROR_INVALID_PARAMETER) return true;

    // Use the same forceful taskkill path as the batch file if direct process
    // termination fails. An absolute system path avoids PATH substitutions.
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) return false;
    const auto executable = fs::path(system) / L"taskkill.exe";
    std::wstring command = L"\"" + executable.wstring() + L"\" /F /PID " + std::to_wstring(pid);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return false;
    const DWORD wait = WaitForSingleObject(child.hProcess, 5000);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &code);
    else { TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 1000); }
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    return wait == WAIT_OBJECT_0 && code == 0;
}

bool stopProcesses(bool explorerOnly, bool* explorerFound = nullptr) {
    DWORD session{};
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return false;
    std::set<DWORD> inputWindows;
    if (!explorerOnly) EnumWindows(cleanupInputWindow, reinterpret_cast<LPARAM>(&inputWindows));
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool ok = true;
    BOOL found = Process32FirstW(snapshot, &entry);
    if (!found && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    while (found) {
        const bool explorer = _wcsicmp(entry.szExeFile, L"explorer.exe") == 0;
        const bool selected = explorer || (!explorerOnly &&
            (std::any_of(std::begin(cleanupImages), std::end(cleanupImages),
                [&](const wchar_t* name) { return _wcsicmp(entry.szExeFile, name) == 0; }) ||
             (_wcsicmp(entry.szExeFile, L"TextInputHost.exe") == 0 && inputWindows.count(entry.th32ProcessID))));
        if (selected && entry.th32ProcessID != GetCurrentProcessId()) {
            DWORD processSession{};
            if (ProcessIdToSessionId(entry.th32ProcessID, &processSession)) {
                if (processSession == session) {
                    if (explorer && explorerFound) *explorerFound = true;
                    if (!forceStop(entry.th32ProcessID) && explorer) ok = false;
                }
            } else if (explorer && GetLastError() != ERROR_INVALID_PARAMETER) ok = false;
        }
        found = Process32NextW(snapshot, &entry);
        if (!found && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    }
    CloseHandle(snapshot);
    return ok;
}

BOOL CALLBACK findNativeTaskbar(HWND window, LPARAM data) {
    wchar_t name[128]{}; GetClassNameW(window, name, 128);
    if (wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) {
        // Hide the old bar immediately, including secondary-monitor bars,
        // while its owner finishes terminating.
        ShowWindow(window, SW_HIDE);
        *reinterpret_cast<bool*>(data) = true;
    }
    return TRUE;
}

bool stopExplorer(bool cleanup = false) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        bool nativeBar = false;
        EnumWindows(findNativeTaskbar, reinterpret_cast<LPARAM>(&nativeBar));
        if (!stopProcesses(!cleanup || attempt != 0)) return false;
        Sleep(200);
        nativeBar = false;
        EnumWindows(findNativeTaskbar, reinterpret_cast<LPARAM>(&nativeBar));
        bool restarted = false;
        if (!stopProcesses(true, &restarted)) return false;
        if (!nativeBar && !restarted) return true;
    }
    return false;
}

fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &value))) result = value;
    CoTaskMemFree(value);
    return result;
}

void launch(const fs::path& path, const wchar_t* args = nullptr) {
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(bar, L"open", path.c_str(), args, nullptr, SW_SHOWNORMAL)) <= 32)
        MessageBoxW(bar, L"Windows could not open this item.", L"Explorer companion", MB_OK | MB_ICONERROR);
}

void openBrowser(const fs::path& folder = {}) {
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, 32768);
    // One distributed executable, independent processes for browser windows.
    const auto args = folder.empty() ? std::wstring(L"--browser") : L"--browser \"" + folder.wstring() + L"\"";
    launch(fs::path(module), args.c_str());
}

bool smokeBrowserProcess() {
    wchar_t module[32768]{};
    if (!GetModuleFileNameW(nullptr, module, 32768)) return false;
    std::wstring command = L"\"" + std::wstring(module) + L"\" --browser-smoke-test";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(module, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) return false;
    const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 2000); }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 && code == 0 && IsWindow(bar);
}

bool isTask(HWND window) {
    explorer::taskbar::WindowTraits traits;
    traits.visible = IsWindowVisible(window) != FALSE;
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    wchar_t name[128]{}; GetClassNameW(window, name, 128);
    traits.excluded = window == bar || window == desktop || window == GetShellWindow() ||
        pid == GetCurrentProcessId() || wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0;
    const auto ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
    traits.tool = (ex & WS_EX_TOOLWINDOW) != 0;
    traits.noActivate = (ex & WS_EX_NOACTIVATE) != 0;
    traits.owned = GetWindow(window, GW_OWNER) != nullptr;
    traits.appWindow = (ex & WS_EX_APPWINDOW) != 0;
    DWORD cloaked{};
    traits.cloaked = SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
    traits.titled = GetWindowTextLengthW(window) > 0;
    return explorer::taskbar::eligible(traits);
}

BOOL CALLBACK collectWindow(HWND window, LPARAM data) {
    if (isTask(window)) {
        wchar_t title[1024]{}; GetWindowTextW(window, title, 1024);
        reinterpret_cast<std::vector<Task>*>(data)->push_back({window, title});
    }
    return TRUE;
}

void refreshTasks() {
    std::vector<Task> current;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&current));
    // Preserve button order when the foreground window changes the Z order.
    std::vector<Task> ordered;
    for (const auto& old : tasks) {
        auto it = std::find_if(current.begin(), current.end(), [&](const Task& t) { return t.window == old.window; });
        if (it != current.end()) { ordered.push_back(*it); current.erase(it); }
    }
    ordered.insert(ordered.end(), current.begin(), current.end());
    tasks = std::move(ordered);
    layoutButtons();
}

void activate(HWND window) {
    if (!IsWindow(window)) return;
    if (window == priorForeground && !IsIconic(window)) ShowWindowAsync(window, SW_MINIMIZE);
    else {
        if (IsIconic(window)) ShowWindowAsync(window, SW_RESTORE);
        if (!SetForegroundWindow(window)) {
            FLASHWINFO flash{sizeof(flash), window, FLASHW_TRAY, 2, 0};
            FlashWindowEx(&flash);
        }
    }
}

void positionBar() {
    if ((!registered && !manualWorkArea) || positioning) return;
    positioning = true;
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = bar; data.uEdge = ABE_BOTTOM; data.rc = monitor.rcMonitor;
    data.rc.top = data.rc.bottom - height;
    if (registered) {
        SHAppBarMessage(ABM_QUERYPOS, &data);
        data.rc.top = data.rc.bottom - height;
        SHAppBarMessage(ABM_SETPOS, &data);
    } else {
        savedWorkArea = monitor.rcMonitor;
        RECT work = savedWorkArea;
        work.bottom = data.rc.top;
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
    }
    SetWindowPos(bar, fullscreen ? HWND_BOTTOM : HWND_TOPMOST, data.rc.left, data.rc.top,
                 data.rc.right - data.rc.left, data.rc.bottom - data.rc.top, SWP_NOACTIVATE);
    positioning = false;
    explorer::background::refresh();
}

bool registerBar() {
    APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = bar; data.uCallbackMessage = AppbarMessage;
    // Explorer normally hosts appbar registration. Reserve the work area
    // ourselves when that service is absent after stopping Explorer.
    registered = SHAppBarMessage(ABM_NEW, &data) != 0;
    manualWorkArea = !registered;
    positionBar();
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    return registered || (GetMonitorInfoW(MonitorFromWindow(bar, MONITOR_DEFAULTTOPRIMARY), &monitor) &&
        monitor.rcWork.bottom == monitor.rcMonitor.bottom - height);
}

void tray(bool remove = false) {
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = bar; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TrayMessage; data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpyW(data.szTip, L"Explorer taskbar companion");
    Shell_NotifyIconW(remove ? NIM_DELETE : NIM_ADD, &data);
}

void readItems(const fs::path& root, bool recursive, std::vector<Item>& items) {
    if (root.empty()) return;
    std::error_code ec;
    auto add = [&](const fs::directory_entry& entry) {
        if (items.size() >= 2000) return;
        auto ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) { return std::towlower(ch); });
        if (recursive && ext != L".lnk" && ext != L".url" && ext != L".appref-ms") return;
        if (!recursive && entry.path().filename() == L"desktop.ini") return;
        items.push_back({recursive ? entry.path().stem().wstring() : entry.path().filename().wstring(), entry.path()});
    };
    if (recursive) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end && items.size() < 2000; it.increment(ec)) add(*it);
    } else {
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) add(*it);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return _wcsicmp(a.label.c_str(), b.label.c_str()) < 0; });
}

std::wstring menuLabel(std::wstring label) {
    size_t pos = 0;
    while ((pos = label.find(L'&', pos)) != std::wstring::npos) { label.insert(pos, 1, L'&'); pos += 2; }
    return label;
}

void showDesktop();
void menu(bool launcher) {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, Files, L"Open file browser");
    AppendMenuW(popup, MF_STRING, Desktop, L"Desktop items");
    AppendMenuW(popup, MF_STRING | (explorer::background::visible() ? MF_CHECKED : MF_UNCHECKED),
        Background, L"Desktop background");
    AppendMenuW(popup, MF_STRING, BackgroundFolder, L"Open background folder");
    if (launcher) {
        apps.clear();
        readItems(knownFolder(FOLDERID_Programs), true, apps);
        readItems(knownFolder(FOLDERID_CommonPrograms), true, apps);
        std::set<fs::path> paths;
        for (const auto& app : apps) paths.insert(app.path);
        for (auto it = menuIcons.begin(); it != menuIcons.end();) {
            if (!paths.count(it->first)) {
                if (it->second.bitmap) DeleteObject(it->second.bitmap);
                it = menuIcons.erase(it);
            } else ++it;
        }
        HMENU programs = CreatePopupMenu();
        // Paginate the catalog so large installations remain navigable.
        for (size_t offset = 0; offset < apps.size(); offset += 30) {
            HMENU page = CreatePopupMenu();
            for (size_t i = offset; i < std::min(offset + 30, apps.size()); ++i)
                AppendMenuW(page, MF_STRING, AppFirst + i, menuLabel(apps[i].label).c_str());
            auto label = apps[offset].label + L" … " + apps[std::min(offset + 29, apps.size() - 1)].label;
            AppendMenuW(programs, MF_POPUP, reinterpret_cast<UINT_PTR>(page), menuLabel(label).c_str());
        }
        if (apps.empty()) AppendMenuW(programs, MF_GRAYED, 0, L"No Start menu shortcuts found");
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(programs), L"Programs");
        AppendMenuW(popup, MF_STRING, 105, L"Windows Settings");
        AppendMenuW(popup, MF_STRING, 106, L"Task Manager");
    }
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, Exit, L"Exit companion");
    POINT point{}; GetCursorPos(&point);
    if (GetFocus() && IsChild(bar, GetFocus())) {
        RECT anchor{}; GetWindowRect(GetFocus(), &anchor); point = {anchor.left, anchor.top};
    }
    SetForegroundWindow(bar);
    UINT command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, bar, nullptr);
    DestroyMenu(popup);
    PostMessageW(bar, WM_NULL, 0, 0);
    if (command >= AppFirst && command < AppFirst + apps.size()) launch(apps[command - AppFirst].path);
    else if (command == Files) openBrowser();
    else if (command == Desktop) showDesktop();
    else if (command == Background) explorer::background::toggle();
    else if (command == BackgroundFolder) {
        const auto folder = explorer::background::folder();
        std::error_code ec;
        if (!folder.empty() && fs::is_directory(folder, ec)) openBrowser(folder);
        else MessageBoxW(bar, L"The background folder could not be created.", L"Explorer companion", MB_OK | MB_ICONERROR);
    }
    else if (command == Exit) DestroyWindow(bar);
    else if (command == 105) launch(L"ms-settings:");
    else if (command == 106) {
        wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
        launch(fs::path(system) / L"Taskmgr.exe");
    }
}

void openDesktopSelection() {
    int index = ListView_GetNextItem(desktopList, -1, LVNI_SELECTED);
    if (index >= 0 && static_cast<size_t>(index) < desktopItems.size()) launch(desktopItems[index].path);
}

LRESULT CALLBACK desktopProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_CREATE) {
        desktopList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL | LVS_AUTOARRANGE,
            0, 0, 500, 400, window, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(desktopList, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ListView_SetExtendedListViewStyle(desktopList, LVS_EX_DOUBLEBUFFER);
        SHFILEINFOW info{};
        auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(L"file.txt", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
            SHGFI_SYSICONINDEX | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES));
        ListView_SetImageList(desktopList, images, LVSIL_NORMAL);
        // The system owns this image list.
        SetWindowLongPtrW(desktopList, GWL_STYLE, GetWindowLongPtrW(desktopList, GWL_STYLE) | LVS_SHAREIMAGELISTS);
        return 0;
    }
    if (message == WM_SIZE) { MoveWindow(desktopList, 0, 0, LOWORD(l), HIWORD(l), TRUE); return 0; }
    if (message == WM_NOTIFY) {
        auto notification = reinterpret_cast<NMHDR*>(l);
        if (notification->hwndFrom == desktopList && (notification->code == NM_DBLCLK || notification->code == NM_RETURN))
            openDesktopSelection();
        return 0;
    }
    if (message == WM_CLOSE) { ShowWindow(window, SW_HIDE); return 0; }
    return DefWindowProcW(window, message, w, l);
}

void showDesktop() {
    desktopItems.clear();
    readItems(knownFolder(FOLDERID_Desktop), false, desktopItems);
    readItems(knownFolder(FOLDERID_PublicDesktop), false, desktopItems);
    if (!desktop) {
        desktop = CreateWindowExW(0, L"RexplorerDesktop", L"Desktop items — Explorer companion",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 450, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    ListView_DeleteAllItems(desktopList);
    for (size_t i = 0; i < desktopItems.size(); ++i) {
        SHFILEINFOW info{};
        SHGetFileInfoW(desktopItems[i].path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_LARGEICON);
        LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_IMAGE; item.iItem = static_cast<int>(i);
        item.pszText = desktopItems[i].label.data(); item.iImage = info.iIcon;
        ListView_InsertItem(desktopList, &item);
    }
    ShowWindow(desktop, SW_RESTORE); SetForegroundWindow(desktop); SetFocus(desktopList);
}

LRESULT CALLBACK taskButtonProc(HWND button, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    HWND target = reinterpret_cast<HWND>(data);
    if (message == WM_LBUTTONDOWN) priorForeground = GetForegroundWindow();
    if (message == WM_CONTEXTMENU && IsWindow(target)) {
        HMENU popup = GetSystemMenu(target, FALSE);
        POINT point{}; GetCursorPos(&point);
        if (l == -1) { RECT rect{}; GetWindowRect(button, &rect); point = {rect.left, rect.top}; }
        SetForegroundWindow(bar);
        const UINT command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, bar, nullptr);
        if (command) PostMessageW(target, WM_SYSCOMMAND, command, 0);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(button, taskButtonProc, id);
    return DefSubclassProc(button, message, w, l);
}

HWND makeButton(int id, const wchar_t* title, bool task = false) {
    HWND button = CreateWindowExW(0, L"BUTTON", title,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | (task ? BS_CHECKBOX | BS_PUSHLIKE : BS_PUSHBUTTON),
        0, 0, 0, 0, bar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return button;
}

void layoutButtons() {
    if (!startButton) return;
    RECT rect{}; GetClientRect(bar, &rect);
    int width = rect.right;
    const auto layout = explorer::taskbar::layout(width, dpi, tasks.size());
    visibleTasks = layout.count;
    buttonWidth = layout.taskWidth;
    while (taskButtons.size() < static_cast<size_t>(visibleTasks))
        taskButtons.push_back(makeButton(WindowFirst + static_cast<int>(taskButtons.size()), L"", true));
    MoveWindow(startButton, 0, 0, layout.start, height, TRUE);
    MoveWindow(filesButton, layout.start, 0, layout.files, height, TRUE);
    MoveWindow(moreButton, width - layout.more - layout.clock, 0, layout.more, height, TRUE);
    MoveWindow(clockButton, width - layout.clock, 0, layout.clock, height, TRUE);
    HWND foreground = GetForegroundWindow();
    for (size_t i = 0; i < taskButtons.size(); ++i) {
        if (i >= static_cast<size_t>(visibleTasks)) { ShowWindow(taskButtons[i], SW_HIDE); continue; }
        SetWindowSubclass(taskButtons[i], taskButtonProc, 1, reinterpret_cast<DWORD_PTR>(tasks[i].window));
        SetWindowTextW(taskButtons[i], menuLabel(tasks[i].title).c_str());
        SendMessageW(taskButtons[i], BM_SETCHECK, tasks[i].window == foreground ? BST_CHECKED : BST_UNCHECKED, 0);
        MoveWindow(taskButtons[i], layout.taskLeft() + static_cast<int>(i) * buttonWidth, 0, buttonWidth, height, TRUE);
        ShowWindow(taskButtons[i], SW_SHOWNOACTIVATE);
    }
    wchar_t time[32]{}; GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, time, 32);
    SetWindowTextW(clockButton, time);
}

void windowMenu() {
    auto snapshot = tasks;
    HMENU popup = CreatePopupMenu();
    for (size_t i = 0; i < snapshot.size(); ++i)
        AppendMenuW(popup, MF_STRING, WindowFirst + i, menuLabel(snapshot[i].title).c_str());
    if (snapshot.empty()) AppendMenuW(popup, MF_GRAYED, 0, L"No windows");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(bar);
    UINT command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, bar, nullptr);
    DestroyMenu(popup);
    if (command >= WindowFirst && command - WindowFirst < snapshot.size()) activate(snapshot[command - WindowFirst].window);
}

LRESULT CALLBACK barProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (taskbarCreated && message == taskbarCreated) {
        // Defer shutdown until outside Explorer's broadcast call.
        SetTimer(window, 2, 200, nullptr);
        return 0;
    }
    if (message == AppbarMessage) {
        if (w == ABN_POSCHANGED) positionBar();
        if (w == ABN_FULLSCREENAPP) {
            fullscreen = l != 0;
            SetWindowPos(window, fullscreen ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }
    switch (message) {
    case WM_INITMENUPOPUP: populateMenuIcons(reinterpret_cast<HMENU>(w)); return 0;
    case WM_SIZE: layoutButtons(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        EndPaint(window, &ps); return 0;
    }
    case WM_MOUSEACTIVATE: priorForeground = GetForegroundWindow(); return MA_NOACTIVATE;
    case WM_TIMER:
        if (w == 2) {
            KillTimer(window, 2);
            if (!stopExplorer()) {
                MessageBoxW(window, L"Windows Explorer restarted and could not be stopped. Close the companion and retry.",
                    L"Explorer", MB_OK | MB_ICONERROR);
                DestroyWindow(window);
                return 0;
            }
            registered = false;
            registerBar();
        }
        refreshTasks(); explorer::background::refresh(); return 0;
    case WM_DISPLAYCHANGE: positionBar(); return 0;
    case WM_SETTINGCHANGE: explorer::background::refresh(); return 0;
    case WM_ACTIVATE: {
        APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = window;
        SHAppBarMessage(ABM_ACTIVATE, &data); break;
    }
    case WM_WINDOWPOSCHANGED: {
        if (registered && !positioning) { APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = window; SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data); }
        break;
    }
    case WM_COMMAND: {
        const int command = LOWORD(w);
        if (command == Start) menu(true);
        else if (command == Files) openBrowser();
        else if (command == More) windowMenu();
        else if (command == Clock) launch(L"ms-settings:dateandtime");
        else if (command >= WindowFirst && command - WindowFirst < visibleTasks)
            activate(tasks[command - WindowFirst].window);
        return 0;
    }
    case WM_CONTEXTMENU: menu(false); return 0;
    case WM_HOTKEY: priorForeground = GetForegroundWindow(); SetForegroundWindow(bar); SetFocus(startButton); return 0;
    case TrayMessage:
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) menu(false);
        else if (l == WM_LBUTTONDBLCLK) openBrowser();
        return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: {
        KillTimer(window, 1); KillTimer(window, 2); UnregisterHotKey(window, 1); tray(true);
        if (registered) { APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = window; SHAppBarMessage(ABM_REMOVE, &data); registered = false; }
        if (manualWorkArea) {
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &savedWorkArea, SPIF_SENDCHANGE);
            manualWorkArea = false;
        }
        clearMenuIcons();
        explorer::background::destroy();
        if (desktop) DestroyWindow(desktop);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(window, message, w, l);
}
}

int runWindowsShell(HINSTANCE instance, bool smokeTest) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RexplorerTaskbarCompanion");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return smokeTest ? 1 : 0; }
    if (!stopExplorer(true)) {
        MessageBoxW(nullptr, L"Explorer could not be stopped. The taskbar will not start while Explorer is still running.",
            L"Explorer", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    MONITORINFO originalMonitor{}; originalMonitor.cbSize = sizeof(originalMonitor);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &originalMonitor);
    // Remove Explorer's old reservation before calculating our taskbar bounds.
    savedWorkArea = originalMonitor.rcMonitor;
    if (!SystemParametersInfoW(SPI_SETWORKAREA, 0, &savedWorkArea, SPIF_SENDCHANGE)) {
        MessageBoxW(nullptr, L"The desktop work area could not be reset.", L"Explorer", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    originalMonitor.rcWork = savedWorkArea;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HDC screen = GetDC(nullptr);
    dpi = static_cast<unsigned>(GetDeviceCaps(screen, LOGPIXELSY)); ReleaseDC(nullptr, screen);
    height = MulDiv(42, dpi, 96);
    NONCLIENTMETRICSW metrics{}; metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font = CreateFontIndirectW(&metrics.lfMessageFont);
    WNDCLASSW cls{}; cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpfnWndProc = barProc; cls.lpszClassName = L"RexplorerTaskbar";
    RegisterClassW(&cls);
    cls.lpfnWndProc = desktopProc; cls.lpszClassName = L"RexplorerDesktop";
    cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW); RegisterClassW(&cls);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, L"RexplorerTaskbar", L"Explorer taskbar companion",
        WS_POPUP, 0, 0, 800, height, nullptr, nullptr, instance, nullptr);
    if (!bar || !registerBar()) {
        MessageBoxW(nullptr, L"The companion could not reserve taskbar space. Run it in a normal Windows desktop session.", L"Explorer", MB_OK | MB_ICONERROR);
        if (bar) DestroyWindow(bar);
        if (SUCCEEDED(com)) CoUninitialize();
        DeleteObject(font);
        CloseHandle(mutex);
        return 1;
    }
    HWND foregroundBeforeBackground = GetForegroundWindow();
    const bool backgroundCreated = explorer::background::create(instance);
    const bool backgroundKeptFocus = GetForegroundWindow() == foregroundBeforeBackground;
    if (!backgroundCreated && !smokeTest)
        MessageBoxW(bar, L"The desktop background could not be created.", L"Explorer companion", MB_OK | MB_ICONERROR);
    startButton = makeButton(Start, L"Start");
    filesButton = makeButton(Files, L"Files");
    moreButton = makeButton(More, L"All");
    clockButton = makeButton(Clock, L"Clock");
    tray(); refreshTasks(); ShowWindow(bar, SW_SHOWNOACTIVATE);
    if (smokeTest) {
        RECT rect{}; GetWindowRect(bar, &rect);
        HWND background = explorer::background::handle();
        RECT backgroundRect{}; GetWindowRect(background, &backgroundRect);
        bool passed = backgroundCreated && backgroundKeptFocus && explorer::background::visible() &&
            !isTask(background) && (GetWindowLongPtrW(background, GWL_EXSTYLE) & WS_EX_NOACTIVATE) &&
            SendMessageW(background, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATEANDEAT;
        explorer::background::toggle();
        passed = passed && !IsWindowVisible(background);
        explorer::background::toggle();
        passed = passed && explorer::background::visible();
        passed = passed && (registered || manualWorkArea) && rect.bottom == originalMonitor.rcMonitor.bottom && startButton && filesButton && moreButton && clockButton &&
            IsWindowVisible(bar) && !isTask(bar) && rect.bottom > rect.top;
        MONITORINFO during{}; during.cbSize = sizeof(during);
        GetMonitorInfoW(MonitorFromWindow(bar, MONITOR_DEFAULTTOPRIMARY), &during);
        passed = passed && during.rcWork.bottom <= rect.top && EqualRect(&backgroundRect, &during.rcWork);
        wchar_t module[32768]{};
        GetModuleFileNameW(nullptr, module, 32768);
        apps = {{L"Icon smoke test", fs::path(module)}};
        HMENU iconMenu = CreatePopupMenu();
        AppendMenuW(iconMenu, MF_STRING, AppFirst, L"Icon smoke test");
        populateMenuIcons(iconMenu);
        MENUITEMINFOW iconItem{}; iconItem.cbSize = sizeof(iconItem); iconItem.fMask = MIIM_BITMAP;
        BITMAP bitmap{};
        passed = GetMenuItemInfoW(iconMenu, 0, TRUE, &iconItem) && iconItem.hbmpItem &&
            GetObjectW(iconItem.hbmpItem, sizeof(bitmap), &bitmap) &&
            bitmap.bmWidth == MulDiv(16, dpi, 96) && bitmap.bmHeight == MulDiv(16, dpi, 96) &&
            applicationIcon(fs::path(module)) == iconItem.hbmpItem && passed;
        DestroyMenu(iconMenu);
        apps.clear();
        passed = smokeBrowserProcess() && passed;
        showDesktop();
        passed = passed && desktop && desktopList && IsWindowVisible(desktop) && !isTask(desktop);
        DestroyWindow(bar);
        MONITORINFO after{}; after.cbSize = sizeof(after);
        GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &after);
        passed = passed && !IsWindow(background) && EqualRect(&originalMonitor.rcWork, &after.rcWork);
        if (SUCCEEDED(com)) CoUninitialize();
        DeleteObject(font); CloseHandle(mutex);
        return passed ? 0 : 1;
    }
    SetTimer(bar, 1, 1000, nullptr);
    RegisterHotKey(bar, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (desktop && message.hwnd == desktopList && message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            openDesktopSelection();
        } else if (!IsDialogMessageW(bar, &message)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    DeleteObject(font);
    CloseHandle(mutex);
    return 0;
}
