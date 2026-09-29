#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filename.h>
#include <wx/imaglist.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/scrolwin.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>
#include <chrono>
#include <memory>
#include <thread>
#include "filesystem_model.h"

namespace fs = std::filesystem;
using explorer::Entry;
wxString text(const fs::path& path) { return wxString::FromUTF8(path.u8string()); }
fs::path pathOf(const wxString& value) { return fs::u8path(value.ToUTF8().data()); }
wxString humanSize(uintmax_t bytes) {
    if (bytes < 1024) return wxString::Format("%llu B", static_cast<unsigned long long>(bytes));
    double value = static_cast<double>(bytes) / 1024;
    const char* units[] = {"KB", "MB", "GB", "TB"};
    int unit = 0;
    while (value >= 1024 && unit < 3) { value /= 1024; ++unit; }
    return wxString::Format("%.1f %s", value, units[unit]);
}
wxString kind(const Entry& entry) {
    if (entry.symlink) return entry.directory ? "Folder shortcut" : "Symbolic link";
    if (entry.directory) return "File folder";
    auto ext = text(entry.path.extension()).Upper();
    return ext.empty() ? "File" : ext.Mid(1) + " file";
}
wxString dateOf(const Entry& entry) {
    if (entry.modified == fs::file_time_type{}) return "—";
    auto time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        entry.modified - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return wxDateTime(std::chrono::system_clock::to_time_t(time)).Format("%d %b %Y, %H:%M");
}
wxDECLARE_EVENT(EVT_SCAN_DONE, wxThreadEvent);
wxDEFINE_EVENT(EVT_SCAN_DONE, wxThreadEvent);
enum { Back = wxID_HIGHEST + 1, Forward, Up, Home, RefreshFolder, NewFolder, Rename,
       CopyPath, Properties, Hidden, Details, Icons, FocusAddress, FocusSearch, StopSearch };

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

    wxButton* button(wxWindow* parent, wxSizer* sizer, const wxString& label,
                     std::function<void()> action, const wxArtID& art = wxEmptyString) {
        auto* b = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBORDER_NONE);
        if (!art.empty()) b->SetBitmap(wxArtProvider::GetBitmap(art, wxART_BUTTON, wxSize(16, 16)));
        b->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
        sizer->Add(b, 0, wxEXPAND | wxTOP | wxBOTTOM, 3);
        return b;
    }
    void section(wxSizer* sizer, const wxString& title) {
        auto* label = new wxStaticText(side, wxID_ANY, title);
        label->SetForegroundColour(wxColour("#244B83"));
        label->SetFont(label->GetFont().Bold());
        sizer->Add(label, 0, wxTOP | wxBOTTOM, 14);
        sizer->Add(new wxStaticLine(side), 0, wxEXPAND | wxBOTTOM, 6);
    }
    void menus() {
        auto* file = new wxMenu;
        file->Append(NewFolder, "New &folder\tCtrl+Shift+N");
        file->Append(Rename, "&Rename\tF2");
        file->Append(CopyPath, "Copy &path\tCtrl+Shift+C");
        file->Append(Properties, "&Properties\tAlt+Enter");
        file->AppendSeparator(); file->Append(wxID_EXIT, "E&xit\tAlt+F4");
        auto* view = new wxMenu;
        view->AppendRadioItem(Details, "&Details"); view->AppendRadioItem(Icons, "Large &icons");
        view->AppendSeparator(); view->AppendCheckItem(Hidden, "Show &hidden files\tCtrl+H");
        view->Append(RefreshFolder, "&Refresh\tF5");
        auto* go = new wxMenu;
        go->Append(Back, "&Back\tAlt+Left"); go->Append(Forward, "&Forward\tAlt+Right");
        go->Append(Up, "&Up one level\tAlt+Up"); go->Append(Home, "&Home\tAlt+Home");
        go->Append(FocusAddress, "&Address bar\tCtrl+L"); go->Append(FocusSearch, "&Search\tCtrl+F");
        auto* help = new wxMenu; help->Append(wxID_ABOUT, "&About Explorer");
        auto* bar = new wxMenuBar;
        bar->Append(file, "&File"); bar->Append(view, "&View"); bar->Append(go, "&Go"); bar->Append(help, "&Help");
        SetMenuBar(bar);
        Bind(wxEVT_MENU, [this](wxCommandEvent& e) { command(e.GetId()); });
    }
    void makeList() {
        list->ClearAll();
        list->SetWindowStyleFlag((iconView ? wxLC_ICON | wxLC_ALIGN_TOP : wxLC_REPORT) | wxLC_SINGLE_SEL | wxBORDER_NONE);
        if (!iconView) {
            list->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
            list->InsertColumn(1, "Date modified", wxLIST_FORMAT_LEFT, 175);
            list->InsertColumn(2, "Type", wxLIST_FORMAT_LEFT, 135);
            list->InsertColumn(3, "Size", wxLIST_FORMAT_RIGHT, 90);
            if (!query.empty()) list->InsertColumn(4, "Location", wxLIST_FORMAT_LEFT, 280);
        }
    }
    int imageFor(const Entry& entry) const {
        if (entry.directory) return 0;
        const auto ext = explorer::lower(entry.path.extension().u8string());
        if (ext == ".exe" || ext == ".sh" || ext == ".app") return 2;
        return 1;
    }
    void render() {
        fs::path selected;
        auto old = selectedEntry(); if (old) selected = old->path;
        // Item data stores the model index, which remains valid during sorting.
        std::sort(entries.begin(), entries.end(), [this](const Entry& a, const Entry& b) {
            if (a.directory != b.directory) return a.directory;
            int cmp = 0;
            if (sortColumn == 1) cmp = a.modified < b.modified ? -1 : a.modified > b.modified;
            else if (sortColumn == 3) cmp = a.size < b.size ? -1 : a.size > b.size;
            else {
                auto key = [this](const Entry& e) {
                    if (sortColumn == 2) return kind(e).Lower().ToStdString();
                    return explorer::lower((sortColumn == 4 ? e.path.parent_path() : e.path.filename()).u8string());
                };
                cmp = key(a).compare(key(b));
            }
            if (!cmp) cmp = a.path.u8string().compare(b.path.u8string());
            return ascending ? cmp < 0 : cmp > 0;
        });
        list->Freeze(); list->DeleteAllItems();
        if (!iconView) {
            if (query.empty() && list->GetColumnCount() == 5) list->DeleteColumn(4);
            if (!query.empty() && list->GetColumnCount() == 4)
                list->InsertColumn(4, "Location", wxLIST_FORMAT_LEFT, 280);
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            long row = list->InsertItem(static_cast<long>(i), text(entry.path.filename()), imageFor(entry));
            list->SetItemData(row, i);
            if (!iconView) {
                list->SetItem(row, 1, dateOf(entry)); list->SetItem(row, 2, kind(entry));
                list->SetItem(row, 3, entry.directory ? wxString{} : humanSize(entry.size));
                if (!query.empty()) list->SetItem(row, 4, text(entry.path.parent_path()));
                if (i % 2) list->SetItemBackgroundColour(row, wxColour("#F5F8FC"));
            }
            if (entry.path == selected) list->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        }
        if (iconView) list->Arrange();
        list->Thaw();
        empty->SetLabel(query.empty() ? "This folder is empty." : "No matching items. Try a different name.");
        empty->Show(entries.empty() && !busy);
        subtitle->SetLabel(query.empty() ? "Select an item to see its details. Double-click to open." : "Search results in this folder and its subfolders");
        SetStatusText(wxString::Format("%zu items", entries.size()), 0);
        selection->SetLabel("Select a file or folder\nto view its details.");
        body->Layout();
    }
    const Entry* selectedEntry() const {
        long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (row < 0) return nullptr;
        auto index = list->GetItemData(row);
        return index < entries.size() ? &entries[index] : nullptr;
    }
    void stop() {
        cancel = true;
        if (worker.joinable()) worker.join();
        ++generation; busy = false;
    }
    void scan() {
        stop(); cancel = false; busy = true;
        const auto ticket = generation;
        const auto root = current;
        const auto term = query.ToStdString(wxConvUTF8);
        const auto hidden = showHidden;
        list->DeleteAllItems(); entries.clear(); empty->Hide();
        selection->SetLabel("Select a file or folder\nto view its details.");
        SetStatusText(query.empty() ? "Loading folder…" : "Searching… Press Esc to stop.", 0);
        heading->SetLabel(query.empty() ? (current.filename().empty() ? text(current) : text(current.filename())) : "Search results");
        body->Layout();
        worker = std::thread([this, root, term, hidden, ticket] {
            auto result = std::make_shared<explorer::ScanResult>(explorer::scan(root, term, hidden, cancel));
            if (cancel) return;
            auto* event = new wxThreadEvent(EVT_SCAN_DONE);
            event->SetInt(static_cast<int>(ticket)); event->SetPayload(result);
            wxQueueEvent(this, event);
        });
    }
    void breadcrumbs() {
        crumbSizer->Clear(true);
        fs::path part = current.root_path();
        auto add = [this](const fs::path& p, const wxString& label) {
            wxString caption = label.length() > 24 ? label.Left(21) + "…" : label;
            caption.Replace("&", "&&");
            auto* b = new wxButton(crumbs, wxID_ANY, caption, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE);
            b->SetToolTip(text(p)); b->Bind(wxEVT_BUTTON, [this, p](wxCommandEvent&) { navigate(p); });
            crumbSizer->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
        };
        add(part, text(part));
        std::vector<fs::path> parts;
        for (const auto& p : current.relative_path()) { part /= p; parts.push_back(part); }
        // Keep the breadcrumb usable for deeply nested paths; the address always shows the full path.
        if (parts.size() > 4) {
            add(parts[parts.size() - 5], "…");
            parts.erase(parts.begin(), parts.end() - 4);
        }
        for (const auto& p : parts) {
            crumbSizer->Add(new wxStaticText(crumbs, wxID_ANY, "›"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
            add(p, text(p.filename()));
        }
        crumbs->Layout();
    }
    void navigate(fs::path target, bool record = true) {
        std::error_code ec;
        target = fs::absolute(target, ec).lexically_normal();
        if (ec || !fs::is_directory(target, ec)) { error("This folder is unavailable:\n" + text(target)); return; }
        // Probe first so an inaccessible directory doesn't become the active location.
        fs::directory_iterator probe(target, ec);
        if (ec) { error(wxString::FromUTF8(ec.message())); return; }
        current = target;
        if (record && (history.empty() || history[historyIndex] != current)) {
            if (!history.empty()) history.resize(historyIndex + 1);
            history.push_back(current); historyIndex = history.size() - 1;
        }
        query.clear(); search->ChangeValue(""); address->ChangeValue(text(current));
        SetTitle((current.filename().empty() ? text(current) : text(current.filename())) + " — Explorer");
        breadcrumbs(); updateNavigation(); scan();
        SetStatusText(text(current), 1);
    }
    void updateNavigation() {
        toolbar->EnableTool(Back, historyIndex > 0);
        toolbar->EnableTool(Forward, historyIndex + 1 < history.size());
        toolbar->EnableTool(Up, current != current.root_path());
        GetMenuBar()->Enable(Back, historyIndex > 0);
        GetMenuBar()->Enable(Forward, historyIndex + 1 < history.size());
    }
    void error(const wxString& message) { wxMessageBox(message, "Explorer", wxOK | wxICON_ERROR, this); }
    void openSelected() {
        auto* entry = selectedEntry(); if (!entry) return;
        if (entry->directory) navigate(entry->path);
        else if (!wxLaunchDefaultApplication(text(entry->path))) error("No application could open this file.");
    }
    void properties() {
        auto* entry = selectedEntry();
        if (!entry) return;
        wxMessageBox("Name: " + text(entry->path.filename()) + "\n\nType: " + kind(*entry) +
                     "\nSize: " + (entry->directory ? "—" : humanSize(entry->size)) +
                     "\nModified: " + dateOf(*entry) + "\n\nLocation: " + text(entry->path),
                     "Properties", wxOK | wxICON_INFORMATION, this);
    }
    void renameSelected() {
        auto* entry = selectedEntry(); if (!entry) return;
        const auto old = entry->path;
        wxTextEntryDialog dialog(this, "Enter a new name:", "Rename", text(old.filename()));
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue() == text(old.filename())) return;
        if (!explorer::validName(dialog.GetValue().ToStdString(wxConvUTF8))) { error("Enter a single file name, without slashes."); return; }
        auto target = old.parent_path() / pathOf(dialog.GetValue());
        std::error_code ec;
        // symlink_status also detects a dangling link, which must never be overwritten.
        auto status = fs::symlink_status(target, ec);
        if (fs::exists(status)) { error("An item with that name already exists."); return; }
        if (ec && ec != std::errc::no_such_file_or_directory) { error(wxString::FromUTF8(ec.message())); return; }
        fs::rename(old, target, ec);
        if (ec) error(wxString::FromUTF8(ec.message())); else scan();
    }
    void newFolder() {
        wxTextEntryDialog dialog(this, "Folder name:", "New folder", "New folder");
        if (dialog.ShowModal() != wxID_OK) return;
        if (!explorer::validName(dialog.GetValue().ToStdString(wxConvUTF8))) { error("Enter a single folder name, without slashes."); return; }
        std::error_code ec;
        if (!fs::create_directory(current / pathOf(dialog.GetValue()), ec))
            error(ec ? wxString::FromUTF8(ec.message()) : "An item with that name already exists.");
        else scan();
    }
    void command(int id) {
        switch (id) {
        case Back: if (historyIndex > 0) {
            const auto index = historyIndex - 1; navigate(history[index], false);
            if (current == history[index]) historyIndex = index;
            updateNavigation();
        } break;
        case Forward: if (historyIndex + 1 < history.size()) {
            const auto index = historyIndex + 1; navigate(history[index], false);
            if (current == history[index]) historyIndex = index;
            updateNavigation();
        } break;
        case Up: navigate(current.parent_path()); break;
        case Home: navigate(pathOf(wxGetHomeDir())); break;
        case RefreshFolder: scan(); break;
        case NewFolder: newFolder(); break;
        case Rename: renameSelected(); break;
        case Properties: properties(); break;
        case CopyPath: {
            auto* e = selectedEntry();
            if (wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(text(e ? e->path : current))); wxTheClipboard->Close(); }
            break;
        }
        case Hidden: showHidden = GetMenuBar()->IsChecked(Hidden); scan(); break;
        case Details: case Icons: iconView = id == Icons; makeList(); render(); break;
        case FocusAddress: address->SetFocus(); address->SelectAll(); break;
        case FocusSearch: search->SetFocus(); search->SelectAll(); break;
        case wxID_EXIT: Close(); break;
        case wxID_ABOUT: wxMessageBox("Explorer\n\nA native, Windows XP-inspired file browser.\nBuilt with wxWidgets and C++17.\n\nSearch matches file names in the current folder\nand all accessible subfolders.", "About Explorer", wxOK | wxICON_INFORMATION, this); break;
        }
    }
public:
    ExplorerFrame(const fs::path& initial) : wxFrame(nullptr, wxID_ANY, "Explorer", wxDefaultPosition, wxSize(1120, 740)) {
        SetMinSize(wxSize(760, 520));
        SetIcon(wxArtProvider::GetIcon(wxART_FOLDER, wxART_FRAME_ICON));
        menus();
        toolbar = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_FLAT);
        toolbar->SetToolBitmapSize(wxSize(24, 24));
        auto tool = [this](int id, const wxString& label, const wxArtID& art) {
            toolbar->AddTool(id, label, wxArtProvider::GetBitmap(art, wxART_TOOLBAR, wxSize(24, 24)), label);
        };
        tool(Back, "Back", wxART_GO_BACK); tool(Forward, "Forward", wxART_GO_FORWARD); tool(Up, "Up", wxART_GO_UP);
        toolbar->AddSeparator(); tool(Home, "Home", wxART_GO_HOME); tool(RefreshFolder, "Refresh", wxART_REDO);
        toolbar->AddSeparator(); tool(NewFolder, "New folder", wxART_NEW_DIR); tool(Properties, "Properties", wxART_INFORMATION);
        toolbar->Realize();
        CreateStatusBar(2); int widths[] = {240, -1}; GetStatusBar()->SetStatusWidths(2, widths);
        auto* root = new wxPanel(this);
        root->SetBackgroundColour(wxColour("#F1F3F7"));
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* addressRow = new wxBoxSizer(wxHORIZONTAL);
        addressRow->Add(new wxStaticText(root, wxID_ANY, "Address"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        address = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        addressRow->Add(address, 1, wxRIGHT, 10);
        search = new wxSearchCtrl(root, wxID_ANY, "", wxDefaultPosition, wxSize(245, -1), wxTE_PROCESS_ENTER);
        search->SetDescriptiveText("Search this folder…"); search->ShowCancelButton(true);
        addressRow->Add(search, 0, wxEXPAND);
        layout->Add(addressRow, 0, wxEXPAND | wxALL, 10);
        crumbs = new wxPanel(root); crumbSizer = new wxBoxSizer(wxHORIZONTAL); crumbs->SetSizer(crumbSizer);
        layout->Add(crumbs, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        auto* split = new wxSplitterWindow(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
        split->SetMinimumPaneSize(190);
        auto* scrollingSide = new wxScrolledWindow(split);
        scrollingSide->SetScrollRate(0, 12);
        side = scrollingSide; side->SetBackgroundColour(wxColour("#DDE8F8"));
        auto* sideOuter = new wxBoxSizer(wxVERTICAL); auto* sideSizer = new wxBoxSizer(wxVERTICAL);
        section(sideSizer, "FILE AND FOLDER TASKS");
        button(side, sideSizer, "Make a new folder", [this] { newFolder(); }, wxART_NEW_DIR);
        button(side, sideSizer, "Rename this item", [this] { renameSelected(); }, wxART_EDIT);
        button(side, sideSizer, "Copy path", [this] { command(CopyPath); }, wxART_COPY);
        section(sideSizer, "OTHER PLACES");
        const auto home = pathOf(wxGetHomeDir());
        button(side, sideSizer, "Home", [this, home] { navigate(home); }, wxART_GO_HOME);
        for (const char* folder : {"Desktop", "Documents", "Downloads", "Pictures", "Music"}) {
            const auto target = home / folder;
            std::error_code ec;
            if (fs::is_directory(target, ec)) button(side, sideSizer, folder, [this, target] { navigate(target); }, wxART_FOLDER);
        }
        button(side, sideSizer, "File system", [this] { navigate(current.root_path()); }, wxART_HARDDISK);
        section(sideSizer, "DETAILS");
        selection = new wxStaticText(side, wxID_ANY, "Select a file or folder\nto view its details.");
        sideSizer->Add(selection, 0, wxEXPAND | wxTOP, 6);
        sideOuter->Add(sideSizer, 1, wxEXPAND | wxALL, 16); side->SetSizer(sideOuter);
        body = new wxPanel(split); body->SetBackgroundColour(*wxWHITE);
        auto* content = new wxBoxSizer(wxVERTICAL);
        heading = new wxStaticText(body, wxID_ANY, "Explorer", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        heading->SetFont(heading->GetFont().Scaled(1.65).Bold()); heading->SetForegroundColour(wxColour("#244B83"));
        content->Add(heading, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 18);
        subtitle = new wxStaticText(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END); subtitle->SetForegroundColour(wxColour("#697586"));
        content->Add(subtitle, 0, wxEXPAND | wxALL, 18);
        content->Add(new wxStaticLine(body), 0, wxEXPAND);
        empty = new wxStaticText(body, wxID_ANY, ""); content->Add(empty, 0, wxALL, 24); empty->Hide();
        list = new wxListCtrl(body, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
        for (int size : {16, 48}) {
            auto* images = new wxImageList(size, size, true);
            for (const auto& art : {wxART_FOLDER, wxART_NORMAL_FILE, wxART_EXECUTABLE_FILE})
                images->Add(wxArtProvider::GetBitmap(art, wxART_OTHER, wxSize(size, size)));
            list->AssignImageList(images, size == 16 ? wxIMAGE_LIST_SMALL : wxIMAGE_LIST_NORMAL);
        }
        makeList(); content->Add(list, 1, wxEXPAND); body->SetSizer(content);
        split->SplitVertically(side, body, 235); layout->Add(split, 1, wxEXPAND);
        root->SetSizer(layout);
        auto* frameSizer = new wxBoxSizer(wxVERTICAL); frameSizer->Add(root, 1, wxEXPAND); SetSizer(frameSizer);
        address->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
            auto value = address->GetValue();
            if (value == "~") value = wxGetHomeDir();
            else if (value.StartsWith("~/")) value = wxGetHomeDir() + value.Mid(1);
            auto target = pathOf(value); navigate(target.is_relative() ? current / target : target);
        });
        auto beginSearch = [this](wxCommandEvent&) { query = search->GetValue().Strip(wxString::both); scan(); };
        search->Bind(wxEVT_TEXT_ENTER, beginSearch); search->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, beginSearch);
        search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { query.clear(); search->ChangeValue(""); scan(); });
        list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { openSelected(); });
        list->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& e) {
            if (sortColumn == e.GetColumn()) ascending = !ascending; else { sortColumn = e.GetColumn(); ascending = true; }
            render();
        });
        list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
            auto* e = selectedEntry(); if (!e) return;
            selection->SetLabel(text(e->path.filename()) + "\n\n" + kind(*e) + "\n" +
                                (e->directory ? wxString{} : humanSize(e->size) + "\n") + "\n" + dateOf(*e));
            selection->Wrap(190); side->Layout();
        });
        list->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& e) {
            list->SetItemState(e.GetIndex(), wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            wxMenu menu; menu.Append(wxID_OPEN, "Open"); menu.Append(Rename, "Rename");
            menu.Append(CopyPath, "Copy path"); menu.AppendSeparator(); menu.Append(Properties, "Properties");
            menu.Bind(wxEVT_MENU, [this](wxCommandEvent& event) { if (event.GetId() == wxID_OPEN) openSelected(); else command(event.GetId()); });
            PopupMenu(&menu);
        });
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
            if (e.GetKeyCode() == WXK_ESCAPE) { query.clear(); search->ChangeValue(""); scan(); }
            else e.Skip();
        });
        Bind(EVT_SCAN_DONE, [this](wxThreadEvent& event) {
            if (static_cast<unsigned>(event.GetInt()) != generation) return;
            if (worker.joinable()) worker.join();
            busy = false;
            auto result = event.GetPayload<std::shared_ptr<explorer::ScanResult>>();
            entries = std::move(result->entries); render();
            if (!result->error.empty()) SetStatusText("Some items could not be read: " + wxString::FromUTF8(result->error), 0);
            if (result->truncated) SetStatusText("Showing first 50,000 matches. Narrow your search.", 0);
        });
        navigate(initial);
        if (current.empty()) navigate(pathOf(wxGetHomeDir()));
        Centre();
    }
    ~ExplorerFrame() override { stop(); }
};
class ExplorerApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Explorer");
        fs::path initial = pathOf(wxGetHomeDir());
        if (argc > 1) initial = pathOf(wxString(argv[1]));
        auto* frame = new ExplorerFrame(initial); frame->Show(); return true;
    }
};
#ifndef EXPLORER_GUI_TEST
wxIMPLEMENT_APP(ExplorerApp);
#endif
