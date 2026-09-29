#pragma once
#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/progdlg.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/scrolwin.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include "../filesystem_model.h"

#include "../platform/services.h"

namespace fs = std::filesystem;
using explorer::Entry;
wxString text(const fs::path& path);
fs::path pathOf(const wxString& value);
wxString humanSize(uintmax_t bytes);
wxString kind(const Entry& entry);
wxString dateOf(const Entry& entry);
wxDECLARE_EVENT(EVT_SCAN_DONE, wxThreadEvent);
enum { Back = wxID_HIGHEST + 1, Forward, Up, Home, RefreshFolder, NewFolder, Rename,
       CopyPath, Properties, Hidden, Details, Icons, FocusAddress, FocusSearch, StopSearch, CutFiles, CopyFiles, PasteFiles, DeleteFiles, NewFile };

class ExplorerFrame final : public wxFrame {
    friend class ExplorerSmokeTest;
    wxPanel *body{}, *crumbs{}, *side{};
    wxBoxSizer* crumbSizer{};
    wxTextCtrl* address{};
    wxSearchCtrl* search{};
    wxListCtrl* list{};
    wxStaticText *heading{}, *subtitle{}, *selection{}, *empty{};
    wxToolBar* toolbar{};
    fs::path current;
    std::vector<fs::path> history;
    size_t historyIndex = 0;
    std::vector<Entry> entries;
    std::thread worker;
    std::atomic<bool> cancel{false};
    unsigned generation = 0;
    bool showHidden = false, iconView = false, ascending = true, busy = false;
    int sortColumn = 0;
    wxString query;
    std::vector<fs::path> cutPaths;
    std::string cutToken;

    wxButton* button(wxWindow* parent, wxSizer* sizer, const wxString& label,
                     std::function<void()> action, const wxArtID& art = wxEmptyString);
    void section(wxSizer* sizer, const wxString& title);
    void menus();
    void makeList();
    int imageFor(const Entry& entry) const;
    void render();
    const Entry* selectedEntry() const;
    void stop();
    void scan();
    void breadcrumbs();
    void navigate(fs::path target, bool record = true);
    void updateNavigation();
    void error(const wxString& message);
    void openSelected();
    void properties();
    void renameSelected();
    void newFolder();
    bool createFileNamed(const wxString& name);
    void newFile();
    void deleteSelected();
    std::vector<fs::path> selectedPaths() const;
    void copyFiles(bool cut);
    void pasteFiles();
    void prepareContextSelection(long row);
    void populateFileMenu(wxMenu& menu);
    void command(int id, bool fileAction = false);
public:
    ExplorerFrame(const fs::path& initial);
    ~ExplorerFrame() override;
};
