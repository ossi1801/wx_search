#include "explorer_frame.h"
#include "archives.h"
#include <wx/filedlg.h>
#include <wx/dirdlg.h>

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
    if (entry.modified == fs::file_time_type{}) return wxS("\u2014");
    auto time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        entry.modified - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return wxDateTime(std::chrono::system_clock::to_time_t(time)).Format("%d %b %Y, %H:%M");
}
wxDEFINE_EVENT(EVT_SCAN_DONE, wxThreadEvent);

wxButton* ExplorerFrame::button(wxWindow* parent, wxSizer* sizer, const wxString& label,
                 std::function<void()> action, const wxArtID& art) {
    auto* b = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBORDER_NONE);
    if (!art.empty()) b->SetBitmap(wxArtProvider::GetBitmap(art, wxART_BUTTON, wxSize(16, 16)));
    b->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
    sizer->Add(b, 0, wxEXPAND | wxTOP | wxBOTTOM, 3);
    return b;
}

void ExplorerFrame::section(wxSizer* sizer, const wxString& title) {
    auto* label = new wxStaticText(side, wxID_ANY, title);
    label->SetForegroundColour(wxColour("#244B83"));
    label->SetFont(label->GetFont().Bold());
    sizer->Add(label, 0, wxTOP | wxBOTTOM, 14);
    sizer->Add(new wxStaticLine(side), 0, wxEXPAND | wxBOTTOM, 6);
}

void ExplorerFrame::menus() {
    auto* file = new wxMenu;
    file->Append(NewFolder, "New &folder\tCtrl+Shift+N");
    file->Append(NewFile, "New &file\tCtrl+N");
    file->Append(DeleteFiles, "&Delete...");
    file->Append(Rename, "&Rename\tF2");
    file->Append(CopyPath, "Copy &path\tCtrl+Shift+C");
    file->Append(Properties, "&Properties\tAlt+Enter");
    file->AppendSeparator(); file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    auto* edit = new wxMenu;
    edit->Append(CutFiles, "Cu&t\tCtrl+X");
    edit->Append(CopyFiles, "&Copy\tCtrl+C");
    edit->Append(PasteFiles, "&Paste\tCtrl+V");
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
    bar->Append(file, "&File"); bar->Append(edit, "&Edit"); bar->Append(view, "&View"); bar->Append(go, "&Go"); bar->Append(help, "&Help");
    SetMenuBar(bar);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { command(e.GetId()); });
}

void ExplorerFrame::makeList() {
    list->ClearAll();
    list->SetWindowStyleFlag((iconView ? wxLC_ICON | wxLC_ALIGN_TOP : wxLC_REPORT) | wxBORDER_NONE);
    if (!iconView) {
        list->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 300);
        list->InsertColumn(1, "Date modified", wxLIST_FORMAT_LEFT, 175);
        list->InsertColumn(2, "Type", wxLIST_FORMAT_LEFT, 135);
        list->InsertColumn(3, "Size", wxLIST_FORMAT_RIGHT, 90);
        if (!query.empty()) list->InsertColumn(4, "Location", wxLIST_FORMAT_LEFT, 280);
    }
}

int ExplorerFrame::imageFor(const Entry& entry) const {
    if (entry.directory) return 0;
    const auto ext = explorer::lower(entry.path.extension().u8string());
    if (ext == ".exe" || ext == ".sh" || ext == ".app") return 2;
    return 1;
}

void ExplorerFrame::render() {
    const auto selected = selectedPaths();
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
        if (std::find(selected.begin(), selected.end(), entry.path) != selected.end()) list->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
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

const Entry* ExplorerFrame::selectedEntry() const {
    long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (row < 0) return nullptr;
    auto index = list->GetItemData(row);
    return index < entries.size() ? &entries[index] : nullptr;
}

void ExplorerFrame::stop() {
    cancel = true;
    if (worker.joinable()) worker.join();
    ++generation; busy = false;
}

void ExplorerFrame::scan() {
    stop(); cancel = false; busy = true;
    const auto ticket = generation;
    const auto root = current;
    const auto term = query.ToStdString(wxConvUTF8);
    const auto hidden = showHidden;
    list->DeleteAllItems(); entries.clear(); empty->Hide();
    selection->SetLabel("Select a file or folder\nto view its details.");
    SetStatusText(query.empty() ? wxS("Loading folder\u2026") : wxS("Searching\u2026 Press Esc to stop."), 0);
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

void ExplorerFrame::breadcrumbs() {
    crumbSizer->Clear(true);
    fs::path part = current.root_path();
    auto add = [this](const fs::path& p, const wxString& label) {
        wxString caption = label.length() > 24 ? label.Left(21) + wxS("\u2026") : label;
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
        add(parts[parts.size() - 5], wxS("\u2026"));
        parts.erase(parts.begin(), parts.end() - 4);
    }
    for (const auto& p : parts) {
        crumbSizer->Add(new wxStaticText(crumbs, wxID_ANY, ">"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
        add(p, text(p.filename()));
    }
    crumbs->Layout();
}

void ExplorerFrame::navigate(fs::path target, bool record) {
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
    SetTitle((current.filename().empty() ? text(current) : text(current.filename())) + wxS(" \u2014 Explorer"));
    breadcrumbs(); updateNavigation(); scan();
    SetStatusText(text(current), 1);
}

void ExplorerFrame::updateNavigation() {
    toolbar->EnableTool(Back, historyIndex > 0);
    toolbar->EnableTool(Forward, historyIndex + 1 < history.size());
    toolbar->EnableTool(Up, current != current.root_path());
    GetMenuBar()->Enable(Back, historyIndex > 0);
    GetMenuBar()->Enable(Forward, historyIndex + 1 < history.size());
}

void ExplorerFrame::error(const wxString& message) { wxMessageBox(message, "Explorer", wxOK | wxICON_ERROR, this); }

void ExplorerFrame::openSelected() {
    auto* entry = selectedEntry(); if (!entry) return;
    if (entry->directory) navigate(entry->path);
    else if (!explorer::platform::openFile(entry->path)) error("No application could open this file.");
}

void ExplorerFrame::properties() {
    auto* entry = selectedEntry();
    if (!entry) return;
    wxMessageBox("Name: " + text(entry->path.filename()) + "\n\nType: " + kind(*entry) +
                 "\nSize: " + (entry->directory ? wxS("\u2014") : humanSize(entry->size)) +
                 "\nModified: " + dateOf(*entry) + "\n\nLocation: " + text(entry->path),
                 "Properties", wxOK | wxICON_INFORMATION, this);
}

void ExplorerFrame::renameSelected() {
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

void ExplorerFrame::newFolder() {
    wxTextEntryDialog dialog(this, "Folder name:", "New folder", "New folder");
    if (dialog.ShowModal() != wxID_OK) return;
    if (!explorer::validName(dialog.GetValue().ToStdString(wxConvUTF8))) { error("Enter a single folder name, without slashes."); return; }
    std::error_code ec;
    if (!fs::create_directory(current / pathOf(dialog.GetValue()), ec))
        error(ec ? wxString::FromUTF8(ec.message()) : "An item with that name already exists.");
    else scan();
}

bool ExplorerFrame::createFileNamed(const wxString& name) {
    if (!explorer::validName(name.ToStdString(wxConvUTF8))) {
        error("Enter a single file name, without slashes."); return false;
    }
    wxFile file;
    if (!file.Create(text(current / pathOf(name)), false)) {
        error("Could not create the file. The name may already exist or the folder may not be writable.");
        return false;
    }
    if (!file.Close()) { error("Could not finish creating the file."); return false; }
    scan(); return true;
}

void ExplorerFrame::newFile() {
    wxTextEntryDialog dialog(this, "File name (you can change the extension):", "New file", "New file.txt");
    if (dialog.ShowModal() == wxID_OK) createFileNamed(dialog.GetValue());
}

void ExplorerFrame::deleteSelected() {
    const auto paths = selectedPaths();
    if (paths.empty()) return;
    explorer::DeletePlan plan;
    try {
        wxProgressDialog progress("Preparing deletion", "Counting files and folders...", 100, this,
                                  wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME);
        plan = explorer::planDelete(paths, [&](size_t count) {
            return count % 128 != 0 || progress.Pulse(wxString::Format("Counting: %zu items", count));
        });
    } catch (const std::exception& e) { error(wxString::FromUTF8(e.what())); return; }
    wxString names;
    for (size_t i = 0; i < paths.size() && i < 10; ++i) names += text(paths[i].filename()) + "\n";
    if (paths.size() > 10) names += wxString::Format("...and %zu more selected items\n", paths.size() - 10);
    const auto message = "Are you sure you want to permanently delete?\n\n" + names +
        wxString::Format("\n%zu items, including selected files/folders and all their contents.\n", plan.items.size()) +
        "Total file size: " + humanSize(plan.bytes) + "\n\nThis cannot be undone. Items will not go to the Trash or Recycle Bin.";
    if (wxMessageBox(message, "Confirm deletion", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) return;
    std::string errors;
    {
        wxProgressDialog progress("Deleting", "Deleting selected items...", 100, this,
                                  wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME);
        errors = explorer::executeDelete(plan, [&](size_t count) {
            return count % 128 != 0 || progress.Update(static_cast<int>(100.0 * count / plan.items.size()),
                wxString::Format("Deleting: %zu of %zu items", count, plan.items.size()));
        });
    }
    scan();
    if (!errors.empty()) error(wxString::FromUTF8(errors));
}

std::vector<fs::path> ExplorerFrame::selectedPaths() const {
    std::vector<fs::path> paths;
    for (long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); row >= 0;
         row = list->GetNextItem(row, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) {
        auto index = list->GetItemData(row);
        if (index < entries.size()) paths.push_back(entries[index].path);
    }
    return paths;
}

void ExplorerFrame::copyFiles(bool cut) {
    auto paths = selectedPaths();
    if (paths.empty()) return;
    if (!wxTheClipboard->Open()) { error("Cannot open the clipboard."); return; }
    auto* data = new wxDataObjectComposite;
    auto* files = new wxFileDataObject;
    for (const auto& path : paths) files->AddFile(text(path));
    data->Add(files, true);
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (cut) {
        auto* marker = new wxCustomDataObject(wxDataFormat("application/x-rexplorer-cut"));
        marker->SetData(token.size(), token.data()); data->Add(marker);
    }
    const bool ok = wxTheClipboard->SetData(data);
    wxTheClipboard->Close();
    if (!ok) { error("Could not put files on the clipboard."); return; }
    cutPaths = cut ? paths : std::vector<fs::path>{}; cutToken = cut ? token : "";
    SetStatusText(wxString::Format("%zu item(s) %s; open a folder and paste.", paths.size(), cut ? "cut" : "copied"));
}

void ExplorerFrame::pasteFiles() {
    if (!wxTheClipboard->Open()) { error("Cannot open the clipboard."); return; }
    wxFileDataObject files;
    const bool available = wxTheClipboard->GetData(files);
    wxCustomDataObject marker(wxDataFormat("application/x-rexplorer-cut"));
    const bool ours = wxTheClipboard->GetData(marker) && !cutToken.empty() &&
        marker.GetSize() == cutToken.size() &&
        std::memcmp(marker.GetData(), cutToken.data(), cutToken.size()) == 0;
    wxTheClipboard->Close();
    if (!available) { SetStatusText("No files on the clipboard."); return; }
    wxBusyCursor cursor;
    wxString errors;
    size_t completed = 0;
    for (const auto& name : files.GetFilenames()) {
        const auto source = pathOf(name);
        auto cut = std::find(cutPaths.begin(), cutPaths.end(), source);
        // A consumed cut entry must never be moved a second time.
        if (ours && cut == cutPaths.end()) continue;
        try {
            explorer::transfer(source, current, ours);
            if (ours) cutPaths.erase(cut);
            ++completed;
        } catch (const std::exception& e) {
            errors += text(source.filename()) + ": " + wxString::FromUTF8(e.what()) + "\n";
        }
    }
    scan();
    if (!errors.empty()) error("Some items could not be pasted (sources retained on copy failure):\n\n" + errors);
    else SetStatusText(wxString::Format("Pasted %zu item(s).", completed));
}

void ExplorerFrame::archiveSelected(bool extract) {
    const auto paths = selectedPaths();
    if (paths.empty()) return;
    fs::path destination;
    if (extract) {
        if (paths.size() != 1 || explorer::lower(paths.front().extension().u8string()) != ".zip") return;
        wxDirDialog dialog(this, "Extract ZIP into a new folder", text(paths.front().parent_path() / paths.front().stem()));
        if (dialog.ShowModal() != wxID_OK) return;
        destination = pathOf(dialog.GetPath());
    } else {
        wxFileDialog dialog(this, "Compress to ZIP", text(current),
                            text(paths.size() == 1 ? paths.front().filename() : fs::path("Archive")) + ".zip",
                            "ZIP archives (*.zip)|*.zip", wxFD_SAVE);
        if (dialog.ShowModal() != wxID_OK) return;
        destination = pathOf(dialog.GetPath());
        if (explorer::lower(destination.extension().u8string()) != ".zip") destination += ".zip";
    }
    try {
        wxProgressDialog progress(extract ? "Extracting ZIP" : "Creating ZIP", "Preparing...", 100, this,
                                  wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME);
        auto update = [&](const fs::path& path) { return progress.Pulse(text(path)); };
        if (extract) explorer::extractZip(paths.front(), destination, update);
        else explorer::createZip(paths, destination, update);
    } catch (const std::exception& exception) { error(wxString::FromUTF8(exception.what())); }
    scan();
}

void ExplorerFrame::prepareContextSelection(long row) {
    if (row >= 0 && !(list->GetItemState(row, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED)) {
        list->SetItemState(-1, 0, wxLIST_STATE_SELECTED);
        list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                           wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
}

void ExplorerFrame::populateFileMenu(wxMenu& menu) {
    const bool selected = selectedEntry() != nullptr;
    menu.Append(wxID_OPEN, "Open"); menu.Append(Rename, "Rename");
    menu.AppendSeparator();
    menu.Append(CutFiles, "Cut\tCtrl+X"); menu.Append(CopyFiles, "Copy\tCtrl+C");
    menu.Append(PasteFiles, "Paste\tCtrl+V");
    menu.AppendSeparator();
    menu.Append(DeleteFiles, "Delete...");
    menu.Append(NewFile, "New file...");
    menu.Append(NewFolder, "New folder"); menu.Append(CopyPath, "Copy path");
    menu.Append(Properties, "Properties");
    menu.AppendSeparator();
    menu.Append(CompressZip, "Compress to ZIP...");
    menu.Append(ExtractZip, "Extract ZIP...");
    menu.Append(OpenContaining, "Open containing folder");
    const auto paths = selectedPaths();
    menu.Enable(CompressZip, selected);
    menu.Enable(ExtractZip, paths.size() == 1 && explorer::lower(paths.front().extension().u8string()) == ".zip");
    menu.Enable(OpenContaining, paths.size() == 1);
    menu.AppendSeparator();
    menu.Append(RefreshFolder, "Refresh\tF5");
    auto* view = new wxMenu;
    view->AppendRadioItem(Details, "Details");
    view->AppendRadioItem(Icons, "Large icons");
    view->Check(iconView ? Icons : Details, true);
    menu.AppendSubMenu(view, "View");
    for (int id : {static_cast<int>(wxID_OPEN), static_cast<int>(Rename), static_cast<int>(CutFiles),
                   static_cast<int>(CopyFiles), static_cast<int>(DeleteFiles), static_cast<int>(Properties)}) menu.Enable(id, selected);
    menu.Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
        if (event.GetId() == wxID_OPEN) openSelected(); else command(event.GetId(), true);
    });
}

void ExplorerFrame::command(int id, bool fileAction) {
    if (!fileAction && (id == CopyFiles || id == CutFiles || id == PasteFiles)) {
        if (auto* input = dynamic_cast<wxTextEntryBase*>(wxWindow::FindFocus())) {
            if (id == CopyFiles) input->Copy();
            else if (id == CutFiles) input->Cut();
            else input->Paste();
            return;
        }
    }
    switch (id) {
    case CopyFiles: copyFiles(false); break;
    case CutFiles: copyFiles(true); break;
    case PasteFiles: pasteFiles(); break;
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
    case Home: navigate(explorer::platform::homeDirectory()); break;
    case RefreshFolder: scan(); break;
    case NewFolder: newFolder(); break;
    case NewFile: newFile(); break;
    case CompressZip: archiveSelected(false); break;
    case ExtractZip: archiveSelected(true); break;
    case OpenContaining: if (auto* entry = selectedEntry()) navigate(entry->path.parent_path()); break;
    case DeleteFiles: deleteSelected(); break;
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

ExplorerFrame::ExplorerFrame(const fs::path& initial) : wxFrame(nullptr, wxID_ANY, "Explorer", wxDefaultPosition, wxSize(1120, 740)) {
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
    toolbar->AddSeparator();
    tool(CutFiles, "Cut", wxART_CUT); tool(CopyFiles, "Copy", wxART_COPY); tool(PasteFiles, "Paste", wxART_PASTE);
    tool(NewFile, "New file", wxART_NEW); tool(DeleteFiles, "Delete", wxART_DELETE);
    toolbar->Realize();
    toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent& event) { command(event.GetId(), true); });
    CreateStatusBar(2); int widths[] = {240, -1}; GetStatusBar()->SetStatusWidths(2, widths);
    auto* root = new wxPanel(this);
    root->SetBackgroundColour(wxColour("#F1F3F7"));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* addressRow = new wxBoxSizer(wxHORIZONTAL);
    addressRow->Add(new wxStaticText(root, wxID_ANY, "Address"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    address = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    addressRow->Add(address, 1, wxRIGHT, 10);
    search = new wxSearchCtrl(root, wxID_ANY, "", wxDefaultPosition, wxSize(245, -1), wxTE_PROCESS_ENTER);
    search->SetDescriptiveText(wxS("Search this folder\u2026")); search->ShowCancelButton(true);
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
    section(sideSizer, "OTHER PLACES");
    const auto home = explorer::platform::homeDirectory();
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
    list = new wxListCtrl(body, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_NONE);
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
        if (value == "~") value = text(explorer::platform::homeDirectory());
        else if (value.StartsWith("~/")) value = text(explorer::platform::homeDirectory()) + value.Mid(1);
        auto target = pathOf(value); navigate(target.is_relative() ? current / target : target);
    });
    auto beginSearch = [this](wxCommandEvent&) { query = search->GetValue().Strip(wxString::both); scan(); };
    search->Bind(wxEVT_TEXT_ENTER, beginSearch); search->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, beginSearch);
    search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { query.clear(); search->ChangeValue(""); scan(); });
    list->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_DELETE && !event.HasAnyModifiers()) deleteSelected();
        else event.Skip();
    });
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
    auto contextMenu = [this](wxContextMenuEvent& event) {
        long row = -1;
        if (event.GetPosition() == wxDefaultPosition) {
            row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
        } else {
            int flags = 0;
            row = list->HitTest(list->ScreenToClient(event.GetPosition()), flags);
        }
        prepareContextSelection(row);
        wxMenu menu;
        populateFileMenu(menu);
        list->SetFocus();
        list->PopupMenu(&menu);
    };
    list->Bind(wxEVT_CONTEXT_MENU, contextMenu);
    body->Bind(wxEVT_CONTEXT_MENU, contextMenu);
    empty->Bind(wxEVT_CONTEXT_MENU, contextMenu);
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
    if (current.empty()) navigate(explorer::platform::homeDirectory());
    Centre();
}

ExplorerFrame::~ExplorerFrame() { stop(); }
