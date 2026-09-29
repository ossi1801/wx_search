#include "../services.h"
#include <wx/utils.h>

namespace explorer::platform {
std::filesystem::path homeDirectory() {
    return std::filesystem::u8path(wxGetHomeDir().ToUTF8().data());
}
bool openFile(const std::filesystem::path& path) {
    return wxLaunchDefaultApplication(wxString::FromUTF8(path.u8string()));
}
}
