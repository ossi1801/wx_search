#define UNICODE
#define _UNICODE
#define NOMINMAX
#include "spotlight.h"
#include "search_model.h"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <condition_variable>
#include <set>
namespace explorer::spotlight {
namespace {
namespace fs = std::filesystem;
constexpr UINT Ready = WM_APP + 71, ResultsReady = WM_APP + 72;
struct Entry { fs::path path; std::wstring label, name, location; bool app; };
HWND window{}, edit{};
HFONT titleFont{}, rowFont{}, smallFont{};
HBRUSH background{};
unsigned scale = 96;
int selected = 0;
std::vector<Entry> index, results;
std::mutex lock;
std::thread worker, matcher;
std::mutex queryLock;
std::condition_variable queryChanged;
std::wstring pendingQuery;
std::atomic<unsigned> generation{0};
std::atomic<unsigned> completedGeneration{0};
std::vector<Entry> completedResults;
std::atomic<bool> stop{false}, indexing{false};
int px(int value) { return MulDiv(value, scale, 96); }
void refresh() {
    wchar_t text[512]{}; GetWindowTextW(edit, text, 512);
    { std::lock_guard<std::mutex> guard(queryLock);
      pendingQuery = search::normalize(text);
      const auto first = pendingQuery.find_first_not_of(L" \t");
      pendingQuery = first == std::wstring::npos ? L"" : pendingQuery.substr(first, pendingQuery.find_last_not_of(L" \t") - first + 1);
      ++generation;
    }
    queryChanged.notify_one();
}
void matchQueries() {
    unsigned processed = 0;
    while (!stop) {
        std::wstring query; unsigned version;
        { std::unique_lock<std::mutex> guard(queryLock);
          queryChanged.wait(guard, [&] { return stop || generation != processed; });
          if (stop) break;
          query = pendingQuery; version = generation;
        }
        std::vector<Entry> found;
        { std::lock_guard<std::mutex> guard(lock);
          std::vector<std::pair<int, const Entry*>> best;
          auto compare = [](const auto& a, const auto& b) {
              return a.first != b.first ? a.first > b.first : a.second->name < b.second->name;
          };
          for (const auto& e : index) {
              if (stop || generation != version) break;
              int rank = search::score(e.name, e.location, query, e.app);
              if (rank < 0 || (query.empty() && !e.app)) continue;
              auto position = std::lower_bound(best.begin(), best.end(), std::make_pair(rank, &e), compare);
              if (position != best.end() || best.size() < 8) {
                  best.insert(position, {rank, &e});
                  if (best.size() > 8) best.pop_back();
              }
          }
          for (const auto& match : best) found.push_back(*match.second);
        }
        processed = version;
        { std::lock_guard<std::mutex> guard(queryLock);
          if (generation != version || stop) continue;
          completedResults = std::move(found); completedGeneration = version;
        }
        PostMessageW(window, ResultsReady, 0, 0);
    }
}
void launch() {
    if (results.empty() || completedGeneration.load() != generation.load()) return;
    const auto path = results[selected].path;
    auto outcome = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (outcome > 32) ShowWindow(window, SW_HIDE);
    else MessageBoxW(window, L"This item could not be opened. It may have moved or been removed.", L"Search", MB_OK | MB_ICONERROR);
}
LRESULT CALLBACK inputProc(HWND control, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (msg == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    if (msg == WM_KEYDOWN) {
        if (w == VK_ESCAPE) { ShowWindow(window, SW_HIDE); return 0; }
        if (w == VK_RETURN) { launch(); return 0; }
        if (w == VK_DOWN || w == VK_UP) {
            if (!results.empty()) selected = (selected + (w == VK_DOWN ? 1 : static_cast<int>(results.size()) - 1)) % results.size();
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
    }
    return DefSubclassProc(control, msg, w, l);
}
void drawText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color) {
    SelectObject(dc, font); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(w) == EN_CHANGE) { results.clear(); selected = 0; InvalidateRect(hwnd, nullptr, FALSE); KillTimer(hwnd, 1); SetTimer(hwnd, 1, 45, nullptr); } return 0;
    case WM_TIMER: KillTimer(hwnd, static_cast<UINT_PTR>(w)); refresh(); return 0;
    case Ready:
        if (IsWindowVisible(hwnd)) SetTimer(hwnd, 2, 120, nullptr);
        return 0;
    case ResultsReady: {
        std::lock_guard<std::mutex> guard(queryLock);
        if (completedGeneration.load() == generation.load()) {
            results = std::move(completedResults); selected = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_CTLCOLOREDIT: SetTextColor(reinterpret_cast<HDC>(w), RGB(244,246,251)); SetBkColor(reinterpret_cast<HDC>(w), RGB(25,28,36)); return reinterpret_cast<LRESULT>(background);
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONUP: {
        int row = (static_cast<short>(HIWORD(l)) - px(88)) / px(56);
        if (static_cast<short>(HIWORD(l)) >= px(88) && row >= 0 && row < static_cast<int>(results.size())) { selected = row; launch(); }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC target = BeginPaint(hwnd, &paint);
        RECT client{}; GetClientRect(hwnd, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP buffer = CreateCompatibleBitmap(target, client.right, client.bottom);
        auto oldBitmap = SelectObject(dc, buffer);
        auto oldFont = SelectObject(dc, rowFont);
        FillRect(dc, &client, background); SetBkMode(dc, TRANSPARENT);
        RECT heading{px(24),px(62),px(610),px(84)};
        drawText(dc, indexing ? L"SEARCH  ·  Updating index…" : L"SEARCH  ·  Apps & files", heading, smallFont, RGB(145,155,175));
        for (size_t i = 0; i < results.size(); ++i) {
            RECT row{px(12),px(88 + static_cast<int>(i)*56),px(628),px(142 + static_cast<int>(i)*56)};
            if (static_cast<int>(i) == selected) { auto brush = CreateSolidBrush(RGB(43,54,76)); FillRect(dc,&row,brush); DeleteObject(brush); }
            RECT name{px(26),row.top+px(4),px(535),row.top+px(29)};
            drawText(dc,results[i].label,name,rowFont,RGB(240,243,250));
            RECT path{px(26),row.top+px(29),px(605),row.bottom-px(3)};
            drawText(dc,results[i].path.parent_path().wstring(),path,smallFont,RGB(149,161,181));
            RECT type{px(540),row.top+px(4),px(610),row.top+px(29)};
            drawText(dc,results[i].app ? L"APP" : L"FILE",type,smallFont,RGB(130,176,250));
        }
        if (results.empty()) { RECT empty{px(26),px(108),px(610),px(155)}; drawText(dc,indexing ? L"Your apps and files will appear here shortly…" : L"No matches. Try another name or folder.",empty,rowFont,RGB(149,161,181)); }
        RECT footer{px(24),px(544),px(615),px(575)};
        drawText(dc,L"↑ ↓  Navigate       Enter  Open       Esc  Dismiss",footer,smallFont,RGB(145,155,175));
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldFont); SelectObject(dc, oldBitmap);
        DeleteObject(buffer); DeleteDC(dc);
        EndPaint(hwnd,&paint); return 0;
    }
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
void buildIndex() {
    const KNOWNFOLDERID* folders[] = {&FOLDERID_Programs,&FOLDERID_CommonPrograms,&FOLDERID_Desktop,&FOLDERID_Documents,&FOLDERID_Downloads,&FOLDERID_Pictures,&FOLDERID_Music,&FOLDERID_Videos};
    std::vector<Entry> batch;
    std::set<fs::path> seen;
    for (size_t root = 0; root < 8 && !stop; ++root) {
        PWSTR raw{};
        if (FAILED(SHGetKnownFolderPath(*folders[root],0,nullptr,&raw))) continue;
        fs::path directory(raw); CoTaskMemFree(raw);
        std::error_code ec;
        fs::recursive_directory_iterator it(directory,fs::directory_options::skip_permission_denied,ec), end;
        size_t visited = 0;
        while (it != end && !stop && visited++ < 50000) {
            auto path = it->path();
            if (it.depth() >= 12 || it->is_symlink(ec)) it.disable_recursion_pending();
            bool isDirectory = it->is_directory(ec);
            auto ext = search::normalize(path.extension().wstring());
            if (!isDirectory && seen.insert(path).second && (root >= 2 || ext == L".lnk" || ext == L".exe" || ext == L".url")) {
                bool app = root < 2 || ext == L".exe" || ext == L".lnk";
                auto label = app ? path.stem().wstring() : path.filename().wstring();
                batch.push_back({path,label,search::normalize(label),search::normalize(path.wstring()),app});
            }
            if (batch.size() >= 256) {
                { std::lock_guard<std::mutex> guard(lock); index.insert(index.end(),batch.begin(),batch.end()); }
                batch.clear(); PostMessageW(window,Ready,0,0);
            }
            it.increment(ec); if (ec) ec.clear();
        }
    }
    { std::lock_guard<std::mutex> guard(lock); index.insert(index.end(),batch.begin(),batch.end()); }
    indexing = false; PostMessageW(window,Ready,0,0);
}
}
bool create(HINSTANCE instance, HWND owner, unsigned dpi) {
    scale = dpi; background = CreateSolidBrush(RGB(25,28,36));
    auto makeFont = [](int size) { return CreateFontW(-px(size),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"); };
    titleFont = makeFont(24); rowFont = makeFont(16); smallFont = makeFont(12);
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = proc; cls.lpszClassName = L"RexplorerSpotlight"; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW); RegisterClassW(&cls);
    window = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"RexplorerSpotlight",L"Search apps and files",WS_POPUP|WS_BORDER,0,0,px(640),px(584),owner,nullptr,instance,nullptr);
    if (!window) { destroy(); return false; }
    int corner = 2; DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
    edit = CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,px(24),px(20),px(590),px(36),window,nullptr,instance,nullptr);
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
    SendMessageW(edit,EM_SETLIMITTEXT,511,0);
    SendMessageW(edit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search apps and files…"));
    SetWindowSubclass(edit,inputProc,1,0);
    if (!edit || !RegisterHotKey(owner,2,MOD_ALT|MOD_NOREPEAT,VK_SPACE)) { destroy(); return false; }
    stop = false; indexing = true; matcher = std::thread(matchQueries); worker = std::thread(buildIndex); return true;
}
void toggle() {
    if (!window) return;
    if (IsWindowVisible(window)) { ShowWindow(window,SW_HIDE); return; }
    POINT cursor{}; GetCursorPos(&cursor); MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
    SetWindowPos(window,HWND_TOPMOST,monitor.rcWork.left + ((monitor.rcWork.right-monitor.rcWork.left)-px(640))/2,monitor.rcWork.top + std::max<LONG>(0,((monitor.rcWork.bottom-monitor.rcWork.top)-px(584))/3),px(640),px(584),SWP_SHOWWINDOW);
    SetForegroundWindow(window); SetFocus(edit); SendMessageW(edit,EM_SETSEL,0,-1); refresh();
}
void destroy() {
    stop = true; queryChanged.notify_all();
    if (worker.joinable()) worker.join();
    if (matcher.joinable()) matcher.join();
    if (window) { UnregisterHotKey(GetWindow(window,GW_OWNER),2); DestroyWindow(window); }
    window = edit = nullptr;
    for (auto font : {titleFont,rowFont,smallFont}) if (font) DeleteObject(font);
    titleFont = rowFont = smallFont = nullptr;
    if (background) DeleteObject(background);
    background = nullptr;
    index.clear(); results.clear();
}
}
