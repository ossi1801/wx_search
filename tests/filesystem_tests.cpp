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
        fs::create_directory(root / "destination");
        transfer(root / "Report.txt", root / "destination", false);
        check(fs::exists(root / "Report.txt") && fs::file_size(root / "destination" / "Report.txt") == 5, "copy must preserve source and content");
        auto rejects = [&](const fs::path& source, const fs::path& destination, bool move) {
            try { transfer(source, destination, move); } catch (const std::exception&) { return true; }
            return false;
        };
        check(rejects(root / "Report.txt", root / "destination", true), "collision must not overwrite on move");
        check(fs::exists(root / "Report.txt"), "failed move retains source");
        check(rejects(root / "nested", root / "nested", false), "reject recursive self-copy");
        check(rejects(root / "Report.txt", root, false), "reject copying onto itself");
        transfer(root / "nested", root / "destination", false);
        check(fs::exists(root / "destination" / "nested" / "REPORT-2026.txt"), "copy directory recursively");
        if (!ec) check(fs::is_symlink(root / "destination" / "nested" / "loop"), "copy must preserve symlinks");
        fs::create_directory(root / "moved");
        transfer(root / "destination" / "nested", root / "moved", true);
        check(!fs::exists(root / "destination" / "nested") && fs::exists(root / "moved" / "nested" / "REPORT-2026.txt"), "cut moves directory and contents");
        check(rejects(root / "missing", root / "destination", false), "missing source reports failure");
        const auto deletion = root / "delete-me";
        fs::create_directories(deletion / "child");
        std::ofstream(deletion / "child" / "data.txt") << "12345";
        std::ofstream(deletion / ".hidden") << "abc";
        auto plan = planDelete({deletion / "child", deletion});
        check(plan.items.size() == 4 && plan.bytes == 8, "deletion counts hidden contents and deduplicates overlapping selections");
        check(fs::exists(deletion / "child" / "data.txt"), "counting must not delete anything");
        bool cancelled = false;
        try { planDelete({deletion}, [](size_t) { return false; }); }
        catch (const std::exception&) { cancelled = true; }
        check(cancelled && fs::exists(deletion), "cancelled counting preserves files");
        check(!executeDelete(plan, [](size_t) { return false; }).empty() && fs::exists(deletion / ".hidden"), "cancel before deletion preserves contents");
        fs::create_directory_symlink(root / "nested", deletion / "link", ec);
        if (!ec) {
            auto linkPlan = planDelete({deletion / "link"});
            check(linkPlan.items.size() == 1 && linkPlan.bytes == 0, "deletion does not follow directory links");
            check(executeDelete(linkPlan).empty() && fs::exists(root / "nested" / "REPORT-2026.txt"), "deleting a link preserves target");
        }
        check(executeDelete(plan).empty() && !fs::exists(deletion), "deletion removes counted tree");
        fs::create_directory(deletion);
        plan = planDelete({deletion});
        std::ofstream(deletion / "arrived-later.txt") << "keep";
        check(!executeDelete(plan).empty() && fs::exists(deletion / "arrived-later.txt"), "new unconfirmed items must survive deletion");
        fs::remove_all(root);
        std::cout << "All filesystem checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n'; return 1;
    }
}
