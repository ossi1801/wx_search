#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/ribbon/buttonbar.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>

class WxSearch : public wxApp
{
public:
    virtual bool OnInit() override;
};

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title);
    void OnAbout(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnTextEnter(wxCommandEvent& event);
    void CreateButton(const std::string &txt);
    void CreateButtonBar();

private:
    wxPanel* panel; // working area
    wxDECLARE_EVENT_TABLE();
	wxTextCtrl* txtBox_Search;
	wxStaticText* label_current_path;
	wxStaticText* label_item_count;
    wxListCtrl *list;

    //Navigational bar
    //wxScrolledWindow* scroll;
    std::vector<wxButton*> buttonBar;
    //wxBoxSizer* sizer;
};

//Filesystem search interface
//bool in_array(const std::string &value, const std::vector<std::string> &array);
std::vector<std::string> search_with_args(std::vector<std::string> args,wxStaticText* label);

enum
{
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_Quit,  MainFrame::OnQuit)
    EVT_MENU(ID_About, MainFrame::OnAbout)
wxEND_EVENT_TABLE()

bool WxSearch::OnInit()
{
    MainFrame* frame = new MainFrame("wxWidgets Hello World");
    frame->Show(true);
    return true;
}
MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(450, 340))
{
    SetSize(800, 600);
    wxMenu* menuFile = new wxMenu;
    menuFile->Append(ID_About, "&About\tCtrl-A", "Show about dialog");
    menuFile->AppendSeparator();
    menuFile->Append(ID_Quit, "E&xit\tAlt-X", "Quit this program");

    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    SetMenuBar(menuBar);
    CreateStatusBar();
    SetStatusText("Wx Search");

    Centre();
	panel = new wxPanel(this);
	txtBox_Search = new wxTextCtrl(panel,wxID_ANY,"", wxPoint(this->m_width-10-200,0),wxSize(200,50),wxTE_LEFT|wxTE_PROCESS_ENTER);
    txtBox_Search->SetHint("Click here to search");
    txtBox_Search->Bind(wxEVT_TEXT_ENTER, &MainFrame::OnTextEnter,this);
    label_current_path = new wxStaticText(panel, wxID_ANY, "",wxPoint(210, 0));

    //result list
    list = new wxListCtrl(panel,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT);
    list->SetSize(790,550);
    list->SetPosition(wxPoint(0,55));
    list->InsertColumn(0, wxString::Format("File"));
    list->SetColumnWidth(0, 600);
    list->InsertColumn(1, wxString::Format("Description"));
    list->SetColumnWidth(1, 200);

    CreateButton("TestiPath");
}

void MainFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxMessageBox("This is a minimal wxWidgets sample\nbuilt cross-platform!",
                 "About Hello World", wxOK | wxICON_INFORMATION);
}

void MainFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    //delete MyApp;
    Close(true);
}
void MainFrame::OnTextEnter(wxCommandEvent &evt){


    wxString text = txtBox_Search->GetValue();

   	if (text.IsEmpty())
    {
         std::cout <<"Text empty"<< std::endl;
    }
    else
    {
		std::cout <<text<< std::endl;
		std::vector<std::string> test;
		test.push_back(text.ToStdString());
		test = search_with_args(test,label_current_path);

        list->DeleteAllItems();
        for (const auto &s : test) {
            list->InsertItem(0,s);
        }
		//wxStdString x;
		//x = wxString::FromUTF8( test[0]	);
		//label->SetLabel(x);

		//std::cout<< wxGetEmailAddress()<<std::endl;
        // Optional: clear after submit (common in chat/input boxes)
        // m_txtInput->Clear();
    }

}

//TODO: create a button bar that shows that allows you to click current path and change it
void MainFrame::CreateButton(const std::string &txt) {
    auto* btn = new wxButton(panel, wxID_ANY, txt);
    btn->Bind(wxEVT_BUTTON, [](wxCommandEvent& e) {
        // e.GetEventObject() returns the button that was clicked
        wxButton* b = static_cast<wxButton*>(e.GetEventObject());
        wxMessageBox("Clicked: " + b->GetLabel());
    });

    buttonBar.push_back(btn);

}



wxIMPLEMENT_APP(WxSearch);



namespace fs = std::filesystem;
bool in_array(const std::string &value, const std::vector<std::string> &array) {
  for (const auto &s : array) {
    if (value.find(s) != std::string::npos)
      return true;
  }
  return false;
}

std::vector<std::string> search_with_args(std::vector<std::string> args, wxStaticText *label) {
    label->SetLabel(fs::current_path().string());
    std::vector<std::string> tmp;
    // Iterate over the std::filesystem::directory_entry elements using `auto`
    auto sanitize = [](std::string wholepath, std::string current_path) {
        int start_position_to_erase = wholepath.find(current_path);
        return wholepath.erase(start_position_to_erase, current_path.length());
    };
    for (auto const &dir_entry:
         fs::recursive_directory_iterator(fs::current_path())) {
        std::string x = dir_entry.path().string();
        std::string x_san =sanitize(x, fs::current_path().string());
        if (in_array(x_san, args))
            tmp.push_back(x_san);
    }
    //if no results show popup
    std::string searchvalues;
    for (int i = 0; i < args.size(); ++i) {
        searchvalues += args[i] + " ";
    }

    if (tmp.empty()) {
        wxMessageBox("No files found with search parameter: \n"+searchvalues,"Info");
    }
    return tmp;
}
