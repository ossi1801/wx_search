#include "browser/archives.h"
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    check(failed, "Operation should have been rejected");
}
int main() {
    auto root = fs::temp_directory_path() / ("explorer-zip-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root / "folder" / "empty");
        std::ofstream(root / "folder" / "résumé.txt") << "hello";
        std::ofstream(root / "other.txt") << "world";
        std::ofstream(root / "folder" / "zero.txt");
        explorer::createZip({root / "folder", root / "other.txt"}, root / "bundle.zip");
        explorer::extractZip(root / "bundle.zip", root / "out");
        check(fs::is_directory(root / "out" / "folder" / "empty"), "Empty folder preserved");
        check(fs::file_size(root / "out" / "folder" / "résumé.txt") == 5, "Unicode file preserved");
        std::string content; std::ifstream(root / "out" / "other.txt") >> content;
        check(content == "world", "File content preserved");
        check(fs::file_size(root / "out" / "folder" / "zero.txt") == 0, "Empty file preserved");
        explorer::extractZip(root / "bundle.zip", root / "new-parent" / "nested-out");
        check(fs::file_size(root / "new-parent" / "nested-out" / "other.txt") == 5,
              "New destination parents created");
        rejects([&] { explorer::createZip({root / "folder"}, root / "bundle.zip"); });
        rejects([&] { explorer::createZip({root / "folder"}, root / "folder" / "self.zip"); });
        rejects([&] { explorer::extractZip(root / "bundle.zip", root / "out"); });
        rejects([&] { explorer::createZip({root / "folder"}, root / "cancel.zip", [](const fs::path&) { return false; }); });
        check(!fs::exists(root / "cancel.zip"), "Cancelled ZIP removed");
        rejects([&] { explorer::extractZip(root / "bundle.zip", root / "cancel-out", [](const fs::path&) { return false; }); });
        check(!fs::exists(root / "cancel-out"), "Cancelled extraction removed");
        for (const auto* name : {"../escape.txt", "C:/escape.txt", "folder/../../escape.txt", "folder\\..\\escape.txt", "CON.txt"}) {
            {
                wxFFileOutputStream file(wxString::FromUTF8((root / "bad.zip").u8string()));
                wxZipOutputStream zip(file);
                zip.PutNextEntry(name); zip.Write("bad", 3); zip.Close(); file.Close();
            }
            rejects([&] { explorer::extractZip(root / "bad.zip", root / "bad-out"); });
            check(!fs::exists(root / "bad-out") && !fs::exists(root / "escape.txt"), "Unsafe ZIP cleaned up");
        }
        std::ofstream(root / "invalid.zip") << "this is not a zip";
        rejects([&] { explorer::extractZip(root / "invalid.zip", root / "invalid-out"); });
        fs::remove_all(root);
        std::cout << "Archive checks passed\n";
    } catch (const std::exception& error) {
        fs::remove_all(root); std::cerr << error.what() << '\n'; return 1;
    }
}
