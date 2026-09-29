#include "filesystem_model.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace explorer;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    const auto root = fs::temp_directory_path() / ("explorer-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root / "nested");
        fs::create_directories(root / ".private");
        std::ofstream(root / "Report.txt") << "hello";
        std::ofstream(root / "nested" / "REPORT-2026.txt") << "world";
        std::ofstream(root / ".private" / "report-secret.txt") << "hidden";
        std::ofstream(root / ".report") << "hidden";
        std::atomic<bool> cancel{false};
        auto plain = scan(root, "", false, cancel);
        check(plain.error.empty() && plain.entries.size() == 2, "folder listing must be shallow and exclude hidden items");
        auto result = scan(root, "report", false, cancel);
        check(result.error.empty() && result.entries.size() == 2, "recursive search must match case-insensitively and skip hidden trees");
        check(result.entries.front().size == 5, "file sizes must be read");
        check(scan(root, "report", true, cancel).entries.size() == 4, "show hidden must include hidden files and subfolders");
        check(scan(root, "no-match", true, cancel).entries.empty(), "unmatched search must be empty");
        std::error_code ec;
        fs::create_directory_symlink(root, root / "nested" / "loop", ec);
        if (!ec) check(scan(root, "report", false, cancel).entries.size() == 2, "search must not follow symlink cycles");
        check(!scan(root / "missing", "", false, cancel).error.empty(), "missing directory must report an error");
        cancel = true;
        check(scan(root, "", true, cancel).entries.empty(), "cancelled scan must stop");
        check(validName("New folder") && validName("résumé.txt"), "normal names must be accepted");
        check(!validName("") && !validName("..") && !validName("../escape") && !validName("a\\b"), "unsafe names must be rejected");
        fs::remove_all(root);
        std::cout << "All filesystem checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n'; return 1;
    }
}
