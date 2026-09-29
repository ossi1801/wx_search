#include "browser/explorer_frame.h"
#ifdef EXPLORER_WINDOWS_SHELL
#include <shellapi.h>
#endif

class ExplorerApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Explorer");
        fs::path initial = explorer::platform::homeDirectory();
        int pathArgument = 1;
#ifdef EXPLORER_WINDOWS_SHELL
        const bool smokeBrowser = argc > 1 && wxString(argv[1]) == "--browser-smoke-test";
        if (argc > 1 && (wxString(argv[1]) == "--browser" || smokeBrowser)) pathArgument = 2;
#endif
        if (argc > pathArgument) initial = pathOf(wxString(argv[pathArgument]));
        auto* frame = new ExplorerFrame(initial); frame->Show();
#ifdef EXPLORER_WINDOWS_SHELL
        if (smokeBrowser) CallAfter([frame] { frame->Close(); });
#endif
        return true;
    }
};
#ifdef EXPLORER_WINDOWS_SHELL
wxIMPLEMENT_APP_NO_MAIN(ExplorerApp);
int runWindowsShell(HINSTANCE instance, bool smokeTest);
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine, int show) {
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool browser = false, smokeTest = false;
    if (arguments && count > 1) {
        const std::wstring mode(arguments[1]);
        browser = mode != L"--shell" && mode != L"--smoke-test";
        smokeTest = mode == L"--smoke-test";
    }
    LocalFree(arguments);
    if (browser) return wxEntry(instance, previous, commandLine, show);
    return runWindowsShell(instance, smokeTest);
}
#else
wxIMPLEMENT_APP(ExplorerApp);
#endif
