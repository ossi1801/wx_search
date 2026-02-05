#include <wx/wx.h>
#include <wx/listctrl.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>

class MyApp : public wxApp
{
public:
    virtual bool OnInit() override;
};

class MyFrame : public wxFrame
{
public:
    MyFrame(const wxString& title);
    void OnAbout(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnTextEnter(wxCommandEvent& event);

private:
    wxDECLARE_EVENT_TABLE();
	wxTextCtrl* textBox;
	wxStaticText* label_current_path;
	wxStaticText* label_item_count;
    wxListCtrl *list;
};

//Filesystem search interface
//bool in_array(const std::string &value, const std::vector<std::string> &array); 
std::vector<std::string> search_with_args(std::vector<std::string> args,wxStaticText* label);

enum
{
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT
};

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_MENU(ID_Quit,  MyFrame::OnQuit)
    EVT_MENU(ID_About, MyFrame::OnAbout)
wxEND_EVENT_TABLE()

bool MyApp::OnInit()
{
    MyFrame* frame = new MyFrame("wxWidgets Hello World");
    frame->Show(true);
    return true;
}
MyFrame::MyFrame(const wxString& title)
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
	wxPanel* panel = new wxPanel(this);
	textBox = new wxTextCtrl(panel,wxID_ANY,"Input field", wxPoint(0,0),wxSize(200,50),wxTE_LEFT|wxTE_PROCESS_ENTER);
    textBox->Bind(wxEVT_TEXT_ENTER, &MyFrame::OnTextEnter,this);
    label_current_path = new wxStaticText(panel, wxID_ANY, "",wxPoint(210, 0));

    //result list
    list = new wxListCtrl(panel,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxLC_REPORT);
    list->SetSize(790,550);
    list->SetPosition(wxPoint(0,55));
    list->InsertColumn(0, wxString::Format("File"));
    list->SetColumnWidth(0, 600);
    list->InsertColumn(1, wxString::Format("Description"));
    list->SetColumnWidth(1, 200);
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxMessageBox("This is a minimal wxWidgets sample\nbuilt cross-platform!",
                 "About Hello World", wxOK | wxICON_INFORMATION);
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    //delete MyApp;
    Close(true);
}
void MyFrame::OnTextEnter(wxCommandEvent &evt){


    wxString text = textBox->GetValue();

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
        // Example actions:
        // - Add to list
        // - Send to server
        // - Process command
        // - etc.

        // Optional: clear after submit (common in chat/input boxes)
        // m_txtInput->Clear();
    }
 
}

wxIMPLEMENT_APP(MyApp);

namespace fs = std::filesystem;
bool in_array(const std::string &value, const std::vector<std::string> &array) {
  for (const auto &s : array) {
    if (value.find(s) != std::string::npos)
      return true;
  }
  return false;
}

std::vector<std::string> search_with_args(std::vector<std::string> args, wxStaticText *label) {
    //for (int i = 0; i < argc; i++) {
    //   std::cout << argv[i] << std::endl;
    //  }
    // sets the path to /tmp
    // std::filesystem::current_path(std::filesystem::temp_directory_path());
    // std::cout << fs::current_path() << std::endl;
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
        if (in_array(x, args))
            tmp.push_back(" ->" + sanitize(x, fs::current_path()));
        else
            tmp.push_back(" X" + sanitize(x, fs::current_path()));
    }
    return tmp;
}
