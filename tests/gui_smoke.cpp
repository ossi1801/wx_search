#include "../browser/explorer_frame.h"
#include <wx/timer.h>
#include <fstream>
#include <iostream>
#ifdef EXPLORER_GTK_SNAPSHOTS
#include <gtk/gtk.h>
#endif

class ExplorerSmokeTest final : public wxApp {
    ExplorerFrame* frame{};
    wxTimer timer;
    fs::path fixture;
    int phase = 0, ticks = 0;
    bool failed = false;
    void require(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }
    void snapshot(const char* name) {
#ifdef EXPLORER_GTK_SNAPSHOTS
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(frame->GetHandle()), &allocation);
        auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, allocation.width, allocation.height);
        auto* cr = cairo_create(surface);
        gtk_widget_draw(GTK_WIDGET(frame->GetHandle()), cr);
        cairo_surface_write_to_png(surface, name);
        cairo_destroy(cr); cairo_surface_destroy(surface);
#else
        (void)name;
#endif
    }
    void menuAction(int id) {
        wxMenu menu;
        frame->populateFileMenu(menu);
        require(menu.FindItem(CopyFiles) && menu.FindItem(CutFiles) && menu.FindItem(PasteFiles), "context menu contains all clipboard actions");
        require(menu.IsEnabled(id), "context action enabled");
        wxCommandEvent event(wxEVT_MENU, id);
        event.SetEventObject(&menu);
        require(menu.ProcessEvent(event), "context command was handled");
    }
    void tick(wxTimerEvent&) {
        try {
            require(++ticks < 150, "GUI scan timed out");
            if (frame->busy) return;
            switch (phase++) {
            case 0:
                require(frame->entries.size() == 3, "initial folder listing");
                snapshot("explorer-details.png");
                frame->navigate(fixture / "Documents"); break;
            case 1:
                require(frame->entries.size() == 1, "nested folder listing");
                frame->command(Back); break;
            case 2:
                require(frame->current == fixture, "back navigation");
                frame->command(Forward); break;
            case 3:
                require(frame->current == fixture / "Documents", "forward navigation");
                frame->command(Up); break;
            case 4:
                frame->query = "report"; frame->scan(); break;
            case 5:
                require(frame->entries.size() == 2, "recursive GUI search");
                frame->GetMenuBar()->Check(Hidden, true); frame->command(Hidden); break;
            case 6:
                require(frame->entries.size() == 3, "hidden results toggle");
                frame->command(Icons); break;
            case 7:
                require(frame->list->GetItemCount() == 3, "icon view preserves results");
                snapshot("explorer-icons.png");
                frame->command(Details);
                frame->navigate(fixture);
                frame->SetSize(760, 520); break;
            case 8:
                snapshot("explorer-small.png");
                frame->navigate(fixture / "Documents");
                frame->navigate(fixture); // Supersede an in-flight worker.
                break;
            case 9:
                require(frame->current == fixture && frame->entries.size() == 4, "stale scan results must not replace current folder");
                for (long row = 0; row < frame->list->GetItemCount(); ++row) {
                    const auto& path = frame->entries[frame->list->GetItemData(row)].path;
                    if (path.filename() == "Project report.txt" || path.filename() == ".report")
                        frame->list->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                }
                frame->prepareContextSelection(frame->list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED));
                // A file context action must ignore stale address-field focus.
                frame->address->SetFocus(); menuAction(CopyFiles);
                frame->navigate(fixture / "Pictures"); break;
            case 10:
                menuAction(PasteFiles); break;
            case 11:
                require(frame->entries.size() == 2 && fs::exists(fixture / "Project report.txt"), "multi-file clipboard copy");
                frame->list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                frame->address->SetFocus(); menuAction(CutFiles);
                frame->navigate(fixture / "Documents"); break;
            case 12: {
                frame->address->SetFocus();
                wxCommandEvent event(wxEVT_TOOL, PasteFiles);
                event.SetEventObject(frame->toolbar);
                require(frame->toolbar->GetEventHandler()->ProcessEvent(event), "toolbar paste handled");
                break;
            }
            case 13:
                require(frame->entries.size() == 2 && frame->cutPaths.empty(), "cut paste moves selected file");
                require(std::distance(fs::directory_iterator(fixture / "Pictures"), fs::directory_iterator{}) == 1, "cut removes source only after paste");
                menuAction(PasteFiles); break;
            case 14:
                require(frame->entries.size() == 2, "consumed cut paste is harmless");
                frame->address->SetFocus(); frame->address->SetValue("text clipboard"); frame->address->SelectAll();
                frame->command(CopyFiles); frame->address->Clear(); frame->command(PasteFiles);
                require(frame->address->GetValue() == "text clipboard", "text controls keep normal clipboard commands");
                require(frame->createFileNamed("New file.txt"), "create default text file"); break;
            case 15:
                require(fs::exists(frame->current / "New file.txt") && fs::file_size(frame->current / "New file.txt") == 0, "new text file is empty");
                require(frame->createFileNamed("custom.json"), "create custom extension"); break;
            case 16: {
                require(fs::exists(frame->current / "custom.json"), "custom extension is preserved");
                wxMenu menu; frame->populateFileMenu(menu);
                require(menu.FindItem(NewFile) && menu.FindItem(DeleteFiles), "new file and delete context actions exist");
                require(!menu.IsEnabled(DeleteFiles), "delete disabled without selection");
                std::cout << "All GUI smoke checks passed\n";
                timer.Stop(); frame->Close(); break;
            }
            }
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n'; failed = true; timer.Stop(); frame->Close();
        }
    }
public:
    bool OnInit() override {
        fixture = fs::temp_directory_path() / ("explorer-gui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) / "Explorer demo";
        fs::create_directories(fixture / "Documents");
        fs::create_directories(fixture / "Pictures");
        std::ofstream(fixture / "Project report.txt") << "A sample document.";
        std::ofstream(fixture / "Documents" / "Annual report.txt") << "Another sample.";
        std::ofstream(fixture / ".report") << "Hidden.";
        frame = new ExplorerFrame(fixture); frame->Show();
        timer.SetOwner(this); Bind(wxEVT_TIMER, &ExplorerSmokeTest::tick, this); timer.Start(250);
        return true;
    }
    int OnExit() override { fs::remove_all(fixture.parent_path()); return failed ? 1 : 0; }
    int OnRun() override { wxApp::OnRun(); return failed ? 1 : 0; }
};
wxIMPLEMENT_APP(ExplorerSmokeTest);
