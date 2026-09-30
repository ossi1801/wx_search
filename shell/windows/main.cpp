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
#include "spotlight.h"

namespace fs = std::filesystem;
namespace {
constexpr UINT AppbarMessage = WM_APP + 1, TrayMessage = WM_APP + 2;
constexpr int Start = 100, Files = 101, Desktop = 102, More = 103, Exit = 104, Clock = 107, Background = 108, BackgroundFolder = 109, Shutdown = 110, Restart = 111, Sound = 112, Network = 113;
constexpr int AppFirst = 1000, WindowFirst = 10000;
struct Item { std::wstring label; fs::path path; };
struct Task { HWND window; std::wstring title; };
HWND bar{}, desktop{}, desktopList{};
std::vector<Task> tasks;
std::vector<Item> apps, desktopItems;
bool registered = false, positioning = false, fullscreen = false;
std::vector<HWND> taskButtons;
HWND startButton{}, filesButton{}, moreButton{}, soundButton{}, networkButton{}, clockButton{};
HICON startIcon{}, filesIcon{}, moreIcon{};
HWND taskTips{};
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
std::map<int, HBITMAP> commandIcons;

void clearMenuIcons() {
    for (const auto& entry : menuIcons) if (entry.second.bitmap) DeleteObject(entry.second.bitmap);
    menuIcons.clear();
    for (const auto& entry : commandIcons) DeleteObject(entry.second);
    commandIcons.clear();
}

HBITMAP iconBitmap(HICON icon);
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
    cached.bitmap = iconBitmap(icon);
    return cached.bitmap;
}

HBITMAP iconBitmap(HICON icon) {
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
    } else if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
    if (dc) DeleteDC(dc);
    DestroyIcon(icon);
    return bitmap;
}

HBITMAP commandIcon(int kind) {
    auto& bitmap = commandIcons[kind];
    if (bitmap) return bitmap;
    if (kind == Shutdown || kind == Restart || kind == 203) {
        const int size = MulDiv(16, dpi, 96);
        BITMAPINFO dib{}; dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        dib.bmiHeader.biWidth = size; dib.bmiHeader.biHeight = -size;
        dib.bmiHeader.biPlanes = 1; dib.bmiHeader.biBitCount = 32;
        DWORD* pixels{};
        HDC dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &dib, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!dc || !bitmap) { if (dc) DeleteDC(dc); if (bitmap) DeleteObject(bitmap); bitmap = nullptr; return nullptr; }
        auto old = SelectObject(dc, bitmap);
        std::fill(pixels, pixels + size * size, 0);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 8), RGB(50, 80, 110));
        auto oldPen = SelectObject(dc, pen);
        auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int edge = std::max(2, size / 5);
        if (kind == Restart) {
            Arc(dc, edge, edge, size-edge, size-edge, size/2, edge, size-edge, size/2);
            MoveToEx(dc, size-edge, edge, nullptr); LineTo(dc, size-edge, size/2); LineTo(dc, size/2, size/2);
        } else {
            Arc(dc, edge, edge, size-edge, size-edge, size/2-edge/2, edge, size/2+edge/2, edge);
            MoveToEx(dc, size/2, 1, nullptr); LineTo(dc, size/2, size/2);
        }
        GdiFlush();
        for (int i = 0; i < size * size; ++i) if (pixels[i] & 0x00ffffff) pixels[i] |= 0xff000000;
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); SelectObject(dc, old);
        DeleteObject(pen); DeleteDC(dc);
        return bitmap;
    }
    SHSTOCKICONID stockId = SIID_APPLICATION;
    if (kind == Files || kind == BackgroundFolder) stockId = SIID_FOLDER;
    else if (kind == Desktop || kind == Background || kind == 201) stockId = SIID_DESKTOPPC;
    else if (kind == 105 || kind == 202) stockId = SIID_SHIELD;
    else if (kind == 106) stockId = SIID_DRIVEFIXED;
    else if (kind == Exit) stockId = SIID_DELETE;
    SHSTOCKICONINFO stock{}; stock.cbSize = sizeof(stock);
    HICON icon{};
    if (SUCCEEDED(SHGetStockIconInfo(stockId, SHGSI_ICON | SHGSI_SMALLICON, &stock))) icon = stock.hIcon;
    if (!icon) icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    if (icon) bitmap = iconBitmap(icon);
    return bitmap;
}

void populateMenuIcons(HMENU popup) {
    for (int i = 0; i < GetMenuItemCount(popup); ++i) {
        wchar_t label[64]{};
        MENUITEMINFOW item{}; item.cbSize = sizeof(item);
        item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_SUBMENU | MIIM_STRING;
        item.dwTypeData = label; item.cch = 64;
        if (!GetMenuItemInfoW(popup, i, TRUE, &item) || (item.fType & MFT_SEPARATOR)) continue;
        HBITMAP bitmap{};
        if (!item.hSubMenu && item.wID >= AppFirst && item.wID < AppFirst + apps.size())
            bitmap = applicationIcon(apps[item.wID - AppFirst].path);
        else {
            int kind = static_cast<int>(item.wID);
            if (item.hSubMenu) {
                kind = 200;
                if (std::wstring(label) == L"Desktop") kind = 201;
                else if (std::wstring(label) == L"System") kind = 202;
                else if (std::wstring(label) == L"Power") kind = 203;
            }
            bitmap = commandIcon(kind);
        }
        item.fMask = MIIM_BITMAP; item.hbmpItem = bitmap;
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

void openVolumeMixer() {
    wchar_t system[MAX_PATH]{};
    if (GetSystemDirectoryW(system, MAX_PATH)) launch(fs::path(system) / L"SndVol.exe");
    else MessageBoxW(bar, L"Windows could not locate the volume mixer.", L"Sound", MB_OK | MB_ICONERROR);
}

void soundMenu(LPARAM position) {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, 1, L"Volume mixer");
    AppendMenuW(popup, MF_STRING, 2, L"Sound settings");
    AppendMenuW(popup, MF_STRING, 3, L"Playback and recording devices");
    POINT point{static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    if (position == -1) {
        RECT rect{}; GetWindowRect(soundButton, &rect);
        point = {rect.left, rect.top};
    }
    SetForegroundWindow(bar);
    const UINT command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, bar, nullptr);
    DestroyMenu(popup);
    if (command == 1) openVolumeMixer();
    else if (command == 2) launch(L"ms-settings:sound");
    else if (command == 3) {
        wchar_t system[MAX_PATH]{};
        if (GetSystemDirectoryW(system, MAX_PATH)) launch(fs::path(system) / L"control.exe", L"mmsys.cpl");
    }
    PostMessageW(bar, WM_NULL, 0, 0);
}

void networkMenu(LPARAM position) {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, 1, L"Network settings");
    AppendMenuW(popup, MF_STRING, 2, L"Wi-Fi settings");
    AppendMenuW(popup, MF_STRING, 3, L"Ethernet settings");
    AppendMenuW(popup, MF_STRING, 4, L"Network adapters");
    POINT point{static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    if (position == -1) {
        RECT rect{}; GetWindowRect(networkButton, &rect);
        point = {rect.left, rect.top};
    }
    SetForegroundWindow(bar);
    const UINT command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, bar, nullptr);
    DestroyMenu(popup);
    if (command == 1) launch(L"ms-settings:network-status");
    else if (command == 2) launch(L"ms-settings:network-wifi");
    else if (command == 3) launch(L"ms-settings:network-ethernet");
    else if (command == 4) {
        wchar_t system[MAX_PATH]{};
        if (GetSystemDirectoryW(system, MAX_PATH)) launch(fs::path(system) / L"control.exe", L"ncpa.cpl");
    }
    PostMessageW(bar, WM_NULL, 0, 0);
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
    AppendMenuW(popup, MF_STRING, Files, L"File browser");
    HMENU desktopMenu = CreatePopupMenu();
    AppendMenuW(desktopMenu, MF_STRING, Desktop, L"Refresh icons");
    AppendMenuW(desktopMenu, MF_STRING | (explorer::background::visible() ? MF_CHECKED : MF_UNCHECKED),
        Background, L"Show background");
    AppendMenuW(desktopMenu, MF_STRING, BackgroundFolder, L"Background folder");
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
        // Alphabetical groups give shortcuts a predictable place in the catalog.
        std::map<wchar_t, HMENU> groups;
        for (size_t i = 0; i < apps.size(); ++i) {
            wchar_t initial = apps[i].label.empty() ? L'#' : std::towupper(apps[i].label.front());
            if (initial < L'A' || initial > L'Z') initial = L'#';
            auto& group = groups[initial];
            if (!group) group = CreatePopupMenu();
            AppendMenuW(group, MF_STRING, AppFirst + i, menuLabel(apps[i].label).c_str());
        }
        for (const auto& group : groups) {
            const std::wstring label(1, group.first);
            AppendMenuW(programs, MF_POPUP, reinterpret_cast<UINT_PTR>(group.second), label.c_str());
        }
        if (apps.empty()) AppendMenuW(programs, MF_GRAYED, 0, L"No Start menu shortcuts found");
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(programs), L"Applications");
    }
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(desktopMenu), L"Desktop");
    HMENU system = CreatePopupMenu();
    AppendMenuW(system, MF_STRING, 105, L"Windows Settings");
    AppendMenuW(system, MF_STRING, 106, L"Task Manager");
    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(system), L"System");
    HMENU power = CreatePopupMenu();
    AppendMenuW(power, MF_STRING, Shutdown, L"Shut down…");
    AppendMenuW(power, MF_STRING, Restart, L"Restart…");
    AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(power), L"Power");
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
    else if (command == Shutdown || command == Restart) {
        const bool restart = command == Restart;
        if (MessageBoxW(bar, restart ? L"Restart this computer now? Save your work before continuing." :
            L"Shut down this computer now? Save your work before continuing.",
            restart ? L"Restart" : L"Shut down", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES) {
            wchar_t systemPath[MAX_PATH]{}; GetSystemDirectoryW(systemPath, MAX_PATH);
            const auto executable = fs::path(systemPath) / L"shutdown.exe";
            auto result = ShellExecuteW(bar, L"open", executable.c_str(), restart ? L"/r /t 0" : L"/s /t 0",
                nullptr, SW_HIDE);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
                MessageBoxW(bar, L"Windows could not start the power action.", L"Power", MB_OK | MB_ICONERROR);
        }
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
        desktopList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL | LVS_AUTOARRANGE | LVS_ALIGNLEFT | LVS_SHAREIMAGELISTS,
            0, 0, 500, 400, window, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(desktopList, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ListView_SetExtendedListViewStyle(desktopList, LVS_EX_DOUBLEBUFFER | LVS_EX_TRANSPARENTBKGND);
        ListView_SetBkColor(desktopList, CLR_NONE);
        ListView_SetTextBkColor(desktopList, CLR_NONE);
        ListView_SetTextColor(desktopList, RGB(255, 255, 255));
        ListView_SetIconSpacing(desktopList, MulDiv(96, dpi, 96), MulDiv(88, dpi, 96));
        SHFILEINFOW info{};
        auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(L"file.txt", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
            SHGFI_SYSICONINDEX | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES));
        ListView_SetImageList(desktopList, images, LVSIL_NORMAL);
        // The system owns this image list.
        SetWindowLongPtrW(desktopList, GWL_STYLE, GetWindowLongPtrW(desktopList, GWL_STYLE) | LVS_SHAREIMAGELISTS);
        return 0;
    }
    if (message == WM_MOUSEACTIVATE) return MA_ACTIVATE;
    if (message == WM_ERASEBKGND || message == WM_PRINTCLIENT)
        return SendMessageW(GetParent(window), WM_PRINTCLIENT, w, PRF_CLIENT);
    if (message == WM_SIZE) { MoveWindow(desktopList, 0, 0, LOWORD(l), HIWORD(l), TRUE); return 0; }
    if (message == WM_NOTIFY) {
        auto notification = reinterpret_cast<NMHDR*>(l);
        if (notification->hwndFrom == desktopList && (notification->code == NM_DBLCLK || notification->code == NM_RETURN))
            openDesktopSelection();
        return 0;
    }
    if (message == WM_CLOSE) return 0;
    if (message == WM_DESTROY) { desktop = nullptr; desktopList = nullptr; return 0; }
    return DefWindowProcW(window, message, w, l);
}

void showDesktop() {
    desktopItems.clear();
    readItems(knownFolder(FOLDERID_Desktop), false, desktopItems);
    readItems(knownFolder(FOLDERID_PublicDesktop), false, desktopItems);
    HWND surface = explorer::background::handle();
    if (!surface) return;
    if (!desktop) {
        RECT client{}; GetClientRect(surface, &client);
        desktop = CreateWindowExW(WS_EX_CONTROLPARENT, L"RexplorerDesktop", L"Desktop icons",
            WS_CHILD | WS_VISIBLE, 0, 0, client.right, client.bottom, surface, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!desktop || !desktopList) return;
    ListView_DeleteAllItems(desktopList);
    for (size_t i = 0; i < desktopItems.size(); ++i) {
        SHFILEINFOW info{};
        SHGetFileInfoW(desktopItems[i].path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_LARGEICON);
        LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_IMAGE; item.iItem = static_cast<int>(i);
        item.pszText = desktopItems[i].label.data(); item.iImage = info.iIcon;
        ListView_InsertItem(desktopList, &item);
    }
    ShowWindow(desktop, SW_SHOWNOACTIVATE);
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

HICON makeStartIcon(int size, bool windows = false) {
    BITMAPINFO dib{}; dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = size; dib.bmiHeader.biHeight = -size;
    dib.bmiHeader.biPlanes = 1; dib.bmiHeader.biBitCount = 32;
    DWORD* pixels = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &dib, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!color) return nullptr;
    std::fill(pixels, pixels + size * size, 0);
    const int stride = ((size + 15) / 16) * 2;
    std::vector<BYTE> maskBits(stride * size, 0xff);
    const int margin = std::max(1, size / 12), gap = std::max(2, size / 12);
    const int pane = (size - 2 * margin - gap) / 2;
    // Four panes form the Start glyph or outlined window-grid glyph at the current DPI.
    for (int row = 0; row < 2; ++row) for (int column = 0; column < 2; ++column) {
        const int left = margin + column * (pane + gap), top = margin + row * (pane + gap);
        for (int y = top; y < top + pane; ++y) for (int x = left; x < left + pane; ++x) {
            if (!windows || x == left || x == left + pane - 1 || y == top || y == top + pane - 1 || y == top + 2)
                pixels[y * size + x] = windows ? 0xff404040 : 0xff0078d7;
            maskBits[y * stride + x / 8] &= static_cast<BYTE>(~(0x80 >> (x % 8)));
        }
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO info{}; info.fIcon = TRUE; info.hbmColor = color; info.hbmMask = mask;
    HICON icon = mask ? CreateIconIndirect(&info) : nullptr;
    if (mask) DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void setLauncherIcons() {
    const int size = MulDiv(24, dpi, 96);
    startIcon = makeStartIcon(size);
    moreIcon = makeStartIcon(size, true);
    SHSTOCKICONINFO stock{}; stock.cbSize = sizeof(stock);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICON | SHGSI_LARGEICON, &stock))) {
        filesIcon = static_cast<HICON>(CopyImage(stock.hIcon, IMAGE_ICON, size, size, 0));
        if (filesIcon) DestroyIcon(stock.hIcon);
        else filesIcon = stock.hIcon;
    }
    if (!filesIcon) filesIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"APP_ICON",
        IMAGE_ICON, size, size, 0));
    HWND tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, bar, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto attach = [&](HWND button, HICON icon, const wchar_t* label) {
        if (icon) {
            SetWindowLongPtrW(button, GWL_STYLE, GetWindowLongPtrW(button, GWL_STYLE) | BS_ICON);
            SendMessageW(button, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(icon));
        }
        // Keep the window text for accessibility; tooltips label the visible icons.
        if (tips) {
            TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.hwnd = bar;
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.uId = reinterpret_cast<UINT_PTR>(button); tool.lpszText = const_cast<wchar_t*>(label);
            SendMessageW(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    };
    attach(startButton, startIcon, L"Start");
    attach(filesButton, filesIcon, L"Files");
    attach(moreButton, moreIcon, L"All windows");
}

HWND makeButton(int id, const wchar_t* title, bool task = false) {
    HWND button = CreateWindowExW(0, L"BUTTON", title,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | (task ? BS_CHECKBOX | BS_PUSHLIKE : BS_PUSHBUTTON),
        0, 0, 0, 0, bar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return button;
}

// Window/class icons are borrowed from the application. Only attach owned copies
// to our buttons, so an application changing or destroying its icon is harmless.
HICON taskIcon(HWND window) {
    const int size = MulDiv(24, dpi, 96);
    for (WPARAM kind : {static_cast<WPARAM>(ICON_SMALL2), static_cast<WPARAM>(ICON_SMALL), static_cast<WPARAM>(ICON_BIG)}) {
        DWORD_PTR result{};
        if (SendMessageTimeoutW(window, WM_GETICON, kind, dpi,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result) && result) {
            if (auto icon = static_cast<HICON>(CopyImage(reinterpret_cast<HICON>(result), IMAGE_ICON, size, size, 0)))
                return icon;
        }
    }
    for (int kind : {GCLP_HICONSM, GCLP_HICON}) {
        auto source = reinterpret_cast<HICON>(GetClassLongPtrW(window, kind));
        if (source) {
            if (auto icon = static_cast<HICON>(CopyImage(source, IMAGE_ICON, size, size, 0))) return icon;
        }
    }
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return nullptr;
    wchar_t path[32768]{}; DWORD length = 32768;
    const bool found = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    if (!found) return nullptr;
    HICON source{};
    if (!ExtractIconExW(path, 0, nullptr, &source, 1) || !source) return nullptr;
    auto icon = static_cast<HICON>(CopyImage(source, IMAGE_ICON, size, size, 0));
    DestroyIcon(source);
    return icon;
}

void setTaskIcon(HWND button, HICON icon) {
    auto style = GetWindowLongPtrW(button, GWL_STYLE);
    SetWindowLongPtrW(button, GWL_STYLE, icon ? style | BS_ICON : style & ~BS_ICON);
    auto previous = reinterpret_cast<HICON>(SendMessageW(button, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(icon)));
    if (previous) DestroyIcon(previous);
    InvalidateRect(button, nullptr, TRUE);
}

void layoutButtons() {
    if (!startButton) return;
    RECT rect{}; GetClientRect(bar, &rect);
    int width = rect.right;
    const auto layout = explorer::taskbar::layout(width, dpi, tasks.size());
    visibleTasks = layout.count;
    buttonWidth = layout.taskWidth;
    if (!taskTips) taskTips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, bar, nullptr, GetModuleHandleW(nullptr), nullptr);
    while (taskButtons.size() < static_cast<size_t>(visibleTasks)) {
        HWND button = makeButton(WindowFirst + static_cast<int>(taskButtons.size()), L"", true);
        taskButtons.push_back(button);
        if (taskTips) {
            TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.hwnd = bar;
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.uId = reinterpret_cast<UINT_PTR>(button); tool.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(taskTips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }
    MoveWindow(startButton, 0, 0, layout.start, height, TRUE);
    MoveWindow(filesButton, layout.start, 0, layout.files, height, TRUE);
    MoveWindow(moreButton, width - layout.more - layout.network - layout.sound - layout.clock, 0, layout.more, height, TRUE);
    MoveWindow(networkButton, width - layout.network - layout.sound - layout.clock, 0, layout.network, height, TRUE);
    MoveWindow(soundButton, width - layout.sound - layout.clock, 0, layout.sound, height, TRUE);
    MoveWindow(clockButton, width - layout.clock, 0, layout.clock, height, TRUE);
    HWND foreground = GetForegroundWindow();
    for (size_t i = 0; i < taskButtons.size(); ++i) {
        if (i >= static_cast<size_t>(visibleTasks)) { setTaskIcon(taskButtons[i], nullptr); ShowWindow(taskButtons[i], SW_HIDE); continue; }
        SetWindowSubclass(taskButtons[i], taskButtonProc, 1, reinterpret_cast<DWORD_PTR>(tasks[i].window));
        SetWindowTextW(taskButtons[i], menuLabel(tasks[i].title).c_str());
        setTaskIcon(taskButtons[i], taskIcon(tasks[i].window));
        SendMessageW(taskButtons[i], BM_SETCHECK, tasks[i].window == foreground ? BST_CHECKED : BST_UNCHECKED, 0);
        MoveWindow(taskButtons[i], layout.taskLeft() + static_cast<int>(i) * buttonWidth, 0, buttonWidth, height, TRUE);
        ShowWindow(taskButtons[i], SW_SHOWNOACTIVATE);
    }
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t dateTime[48]{};
    swprintf(dateTime, 48, L"%02u:%02u\n%02u.%02u.%04u",
        now.wHour, now.wMinute, now.wDay, now.wMonth, now.wYear);
    SetWindowTextW(clockButton, dateTime);
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
    case WM_NOTIFY: {
        auto header = reinterpret_cast<NMHDR*>(l);
        if (header->hwndFrom == taskTips && header->code == TTN_GETDISPINFOW) {
            auto info = reinterpret_cast<NMTTDISPINFOW*>(l);
            static wchar_t title[1024];
            const auto button = reinterpret_cast<HWND>(header->idFrom);
            const auto it = std::find(taskButtons.begin(), taskButtons.end(), button);
            const auto index = static_cast<size_t>(it - taskButtons.begin());
            if (it != taskButtons.end() && index < tasks.size()) {
                lstrcpynW(title, tasks[index].title.c_str(), 1024);
                info->lpszText = title;
            }
            return 0;
        }
        break;
    }
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
        else if (command == Sound) openVolumeMixer();
        else if (command == Network) launch(L"ms-settings:network-status");
        else if (command == Clock) launch(L"ms-settings:dateandtime");
        else if (command >= WindowFirst && command - WindowFirst < visibleTasks)
            activate(tasks[command - WindowFirst].window);
        return 0;
    }
    case WM_CONTEXTMENU:
        if (reinterpret_cast<HWND>(w) == soundButton) soundMenu(l);
        else if (reinterpret_cast<HWND>(w) == networkButton) networkMenu(l);
        else menu(false);
        return 0;
    case WM_HOTKEY: if (w == 2) { explorer::spotlight::toggle(); return 0; } priorForeground = GetForegroundWindow(); SetForegroundWindow(bar); SetFocus(startButton); return 0;
    case TrayMessage:
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) menu(false);
        else if (l == WM_LBUTTONDBLCLK) openBrowser();
        return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: {
        explorer::spotlight::destroy();
        KillTimer(window, 1); KillTimer(window, 2); UnregisterHotKey(window, 1); tray(true);
        if (registered) { APPBARDATA data{}; data.cbSize = sizeof(data); data.hWnd = window; SHAppBarMessage(ABM_REMOVE, &data); registered = false; }
        if (manualWorkArea) {
            SystemParametersInfoW(SPI_SETWORKAREA, 0, &savedWorkArea, SPIF_SENDCHANGE);
            manualWorkArea = false;
        }
        SendMessageW(startButton, BM_SETIMAGE, IMAGE_ICON, 0);
        SendMessageW(filesButton, BM_SETIMAGE, IMAGE_ICON, 0);
        SendMessageW(moreButton, BM_SETIMAGE, IMAGE_ICON, 0);
        if (moreIcon) DestroyIcon(moreIcon);
        if (startIcon) DestroyIcon(startIcon);
        if (filesIcon) DestroyIcon(filesIcon);
        startIcon = filesIcon = moreIcon = nullptr;
        for (HWND button : taskButtons) setTaskIcon(button, nullptr);
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
    INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
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
    cls.hbrBackground = nullptr; RegisterClassW(&cls);
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
    showDesktop();
    const bool backgroundKeptFocus = GetForegroundWindow() == foregroundBeforeBackground;
    if (!backgroundCreated && !smokeTest)
        MessageBoxW(bar, L"The desktop background could not be created.", L"Explorer companion", MB_OK | MB_ICONERROR);
    startButton = makeButton(Start, L"Start");
    filesButton = makeButton(Files, L"Files");
    moreButton = makeButton(More, L"All windows");
    soundButton = makeButton(Sound, L"Sound");
    networkButton = makeButton(Network, L"Network");
    setLauncherIcons();
    clockButton = makeButton(Clock, L"Clock");
    SetWindowLongPtrW(clockButton, GWL_STYLE, GetWindowLongPtrW(clockButton, GWL_STYLE) | BS_MULTILINE);
    tray(); refreshTasks(); ShowWindow(bar, SW_SHOWNOACTIVATE);
    if (!explorer::spotlight::create(instance, bar, dpi) && !smokeTest)
        MessageBoxW(bar, L"Search could not start. Alt+Space may already be registered by another application.", L"Search", MB_OK | MB_ICONWARNING);
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
        passed = passed && (registered || manualWorkArea) && rect.bottom == originalMonitor.rcMonitor.bottom && startButton && filesButton && moreButton && soundButton && networkButton && clockButton &&
            IsWindowVisible(bar) && !isTask(bar) && rect.bottom > rect.top;
        passed = passed && startIcon && filesIcon && moreIcon &&
            SendMessageW(startButton, BM_GETIMAGE, IMAGE_ICON, 0) == reinterpret_cast<LRESULT>(startIcon) &&
            SendMessageW(filesButton, BM_GETIMAGE, IMAGE_ICON, 0) == reinterpret_cast<LRESULT>(filesIcon) &&
            SendMessageW(moreButton, BM_GETIMAGE, IMAGE_ICON, 0) == reinterpret_cast<LRESULT>(moreIcon) &&
            (GetWindowLongPtrW(clockButton, GWL_STYLE) & BS_MULTILINE);
        HWND iconProbe = makeButton(WindowFirst - 1, L"Icon probe", true);
        SendMessageW(iconProbe, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(filesIcon));
        HICON probeIcon = taskIcon(iconProbe);
        setTaskIcon(iconProbe, probeIcon);
        passed = passed && probeIcon && probeIcon != filesIcon &&
            (GetWindowLongPtrW(iconProbe, GWL_STYLE) & BS_ICON) &&
            SendMessageW(iconProbe, BM_GETIMAGE, IMAGE_ICON, 0) == reinterpret_cast<LRESULT>(probeIcon);
        setTaskIcon(iconProbe, nullptr);
        passed = passed && !(GetWindowLongPtrW(iconProbe, GWL_STYLE) & BS_ICON) &&
            !SendMessageW(iconProbe, BM_GETIMAGE, IMAGE_ICON, 0);
        SendMessageW(iconProbe, WM_SETICON, ICON_SMALL, 0);
        DestroyWindow(iconProbe);
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
        AppendMenuW(iconMenu, MF_STRING, Files, L"Files");
        AppendMenuW(iconMenu, MF_STRING, Shutdown, L"Shut down");
        AppendMenuW(iconMenu, MF_STRING, Restart, L"Restart");
        HMENU category = CreatePopupMenu();
        AppendMenuW(iconMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(category), L"Power");
        populateMenuIcons(iconMenu);
        for (int i = 1; i < GetMenuItemCount(iconMenu); ++i) {
            iconItem.hbmpItem = nullptr;
            passed = GetMenuItemInfoW(iconMenu, i, TRUE, &iconItem) && iconItem.hbmpItem && passed;
        }
        DestroyMenu(iconMenu);
        apps.clear();
        passed = smokeBrowserProcess() && passed;
        showDesktop();
        RECT desktopRect{}; GetWindowRect(desktop, &desktopRect);
        passed = passed && desktop && desktopList && IsWindowVisible(desktop) && !isTask(desktop) &&
            GetParent(desktop) == background && (GetWindowLongPtrW(desktop, GWL_STYLE) & WS_CHILD) &&
            !(GetWindowLongPtrW(desktop, GWL_STYLE) & WS_CAPTION) && EqualRect(&desktopRect, &backgroundRect);
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
