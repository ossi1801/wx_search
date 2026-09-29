#define UNICODE
#define _UNICODE
#define NOMINMAX
#include "background.h"
#include "background_images.h"
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <new>

namespace explorer::background {
namespace {
HWND surface{};
bool enabled = true;
ImageFile currentImage;
std::vector<BYTE> pixels;
BITMAPINFO imageInfo{};
ULONGLONG lastImageCheck{};
bool checkedImage = false;

bool loadImage(const fs::path& path) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT width{}, height{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(frame->GetSize(&width, &height))) return false;
    // Bound decoding memory and reject dimensions that overflow the DIB or WIC stride.
    constexpr UINT maxBytes = 256 * 1024 * 1024;
    if (!width || !height || width > maxBytes / 4 || height > maxBytes / (width * 4)) return false;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    std::vector<BYTE> decoded;
    try { decoded.resize(static_cast<size_t>(width) * height * 4); }
    catch (const std::bad_alloc&) { return false; }
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(decoded.size()), decoded.data())))
        return false;
    imageInfo = {};
    imageInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    imageInfo.bmiHeader.biWidth = static_cast<LONG>(width);
    imageInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
    imageInfo.bmiHeader.biPlanes = 1;
    imageInfo.bmiHeader.biBitCount = 32;
    imageInfo.bmiHeader.biCompression = BI_RGB;
    pixels = std::move(decoded);
    return true;
}

void refreshImage() {
    const ULONGLONG now = GetTickCount64();
    if (checkedImage && now - lastImageCheck < 1000) return;
    lastImageCheck = now;
    const auto selected = selectImage(folder());
    if (checkedImage && selected == currentImage) return;
    checkedImage = true;
    currentImage = selected;
    pixels.clear();
    imageInfo = {};
    if (!selected.path.empty()) loadImage(selected.path);
    InvalidateRect(surface, nullptr, FALSE);
}

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
        if (!pixels.empty()) {
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, nullptr);
            const int drawn = StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0,
                imageInfo.bmiHeader.biWidth, -imageInfo.bmiHeader.biHeight,
                pixels.data(), &imageInfo, DIB_RGB_COLORS, SRCCOPY);
            if (drawn != 0 && static_cast<DWORD>(drawn) != GDI_ERROR) { EndPaint(window, &paint); return 0; }
        }
        const int height = std::max(1L, client.bottom);
        // Blue placeholder when no supported image can be decoded.
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

fs::path folder() {
    static const fs::path root = [] {
        PWSTR local = nullptr;
        fs::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
            result = fs::path(local) / L"Rexplorer" / L"Backgrounds";
        CoTaskMemFree(local);
        return result;
    }();
    if (!root.empty()) {
        std::error_code ec;
        fs::create_directories(root, ec);
    }
    return root;
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
    refreshImage();
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) return;
    HWND after = HWND_TOP;
    EnumWindows(findLastApplication, reinterpret_cast<LPARAM>(&after));
    RECT current{}; GetWindowRect(surface, &current);
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
    if (EqualRect(&current, &monitor.rcWork)) flags |= SWP_NOMOVE | SWP_NOSIZE;
    if (GetWindow(surface, GW_HWNDPREV) == after) flags |= SWP_NOZORDER;
    if (!EqualRect(&current, &monitor.rcWork)) InvalidateRect(surface, nullptr, FALSE);
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
    pixels.clear();
    imageInfo = {};
    currentImage = {};
    checkedImage = false;
    lastImageCheck = 0;
}
bool visible() { return surface && enabled && IsWindowVisible(surface); }
HWND handle() { return surface; }
}
