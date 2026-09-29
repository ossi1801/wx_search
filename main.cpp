#include "browser/explorer_frame.h"

class ExplorerApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Explorer");
        fs::path initial = explorer::platform::homeDirectory();
        if (argc > 1) initial = pathOf(wxString(argv[1]));
        auto* frame = new ExplorerFrame(initial); frame->Show(); return true;
    }
};
wxIMPLEMENT_APP(ExplorerApp);
