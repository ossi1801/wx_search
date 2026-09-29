#define EXPLORER_GUI_TEST
#include "../main.cpp"
#include <wx/timer.h>
#include <fstream>
#include <iostream>
#ifdef __WXGTK__
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
#ifdef __WXGTK__
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
                std::cout << "All GUI smoke checks passed\n";
                timer.Stop(); frame->Close(); break;
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
