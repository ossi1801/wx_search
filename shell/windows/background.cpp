#define UNICODE
#define _UNICODE
#define NOMINMAX
#include "background.h"
#include <algorithm>

namespace explorer::background {
namespace {
HWND surface{};
bool enabled = true;

// Keep normal application windows above the surface. Explorer's desktop hosts
// are deliberately excluded so this companion can cover the existing desktop.
// Host class names are a compatibility heuristic, not a shell-hosting contract.
BOOL CALLBACK findLastApplication(HWND window, LPARAM value) {
    if (window == surface || window == GetShellWindow() || !IsWindowVisible(window) || IsIconic(window)) return TRUE;
    if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) return TRUE;
    wchar_t name[128]{}; GetClassNameW(window, name, 128);
    if (wcscmp(name, L"Progman") == 0 || wcscmp(name, L"WorkerW") == 0 ||
        wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) return TRUE;
    *reinterpret_cast<HWND*>(value) = window;
    return TRUE;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATEANDEAT;
    case WM_CONTEXTMENU:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_CLOSE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client);
        const int height = std::max(1L, client.bottom);
        // GDI-only blue wallpaper: no assets, controls, network or animations.
        for (int y = paint.rcPaint.top; y < paint.rcPaint.bottom; y += 2) {
            const int red = 24 + 18 * y / height;
            const int green = 54 + 43 * y / height;
            const int blue = 94 + 48 * y / height;
            SetDCBrushColor(dc, RGB(red, green, blue));
            RECT band{paint.rcPaint.left, y, paint.rcPaint.right, std::min(y + 2, static_cast<int>(paint.rcPaint.bottom))};
            FillRect(dc, &band, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        EndPaint(window, &paint); return 0;
    }
    }
    return DefWindowProcW(window, message, w, l);
}
}

bool create(HINSTANCE instance) {
    if (surface) return true;
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = windowProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.lpszClassName = L"RexplorerBackground";
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    surface = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"Explorer background",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!surface) return false;
    refresh();
    return true;
}

void refresh() {
    if (!surface || !enabled) return;
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) return;
    HWND after = HWND_TOP;
    EnumWindows(findLastApplication, reinterpret_cast<LPARAM>(&after));
    RECT current{}; GetWindowRect(surface, &current);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
    if (EqualRect(&current, &monitor.rcWork)) flags |= SWP_NOMOVE | SWP_NOSIZE;
    if (GetWindow(surface, GW_HWNDPREV) == after) flags |= SWP_NOZORDER;
    SetWindowPos(surface, after, monitor.rcWork.left, monitor.rcWork.top,
        monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top, flags);
}

void toggle() {
    if (!surface) return;
    enabled = !enabled;
    if (enabled) refresh();
    else ShowWindow(surface, SW_HIDE);
}

void destroy() {
    if (surface) DestroyWindow(surface);
    surface = nullptr;
    enabled = true;
}
bool visible() { return surface && enabled && IsWindowVisible(surface); }
HWND handle() { return surface; }
}
