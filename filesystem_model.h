#pragma once
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <vector>
#include <set>
#include <functional>

namespace explorer {
namespace fs = std::filesystem;
struct Entry {
    fs::path path;
    bool directory = false;
    bool symlink = false;
    uintmax_t size = 0;
    fs::file_time_type modified{};
};
inline std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
inline bool hidden(const fs::path& path) {
    const auto name = path.filename().u8string();
    return !name.empty() && name.front() == '.';
}
inline Entry readEntry(const fs::directory_entry& item) {
    std::error_code ec;
    Entry entry;
    entry.path = item.path();
    entry.directory = item.is_directory(ec);
    entry.symlink = item.is_symlink(ec);
    if (!entry.directory && item.is_regular_file(ec)) {
        entry.size = item.file_size(ec);
        if (ec) entry.size = 0;
    }
    entry.modified = item.last_write_time(ec);
    return entry;
}
struct ScanResult {
    std::vector<Entry> entries;
    std::string error;
    bool truncated = false;
};
inline ScanResult scan(const fs::path& root, const std::string& query, bool showHidden,
                       const std::atomic<bool>& cancel) {
    ScanResult result;
    std::error_code ec;
    const auto needle = lower(query);
    auto collect = [&](const fs::directory_entry& item) {
        if ((showHidden || !hidden(item.path())) &&
            (needle.empty() || lower(item.path().filename().u8string()).find(needle) != std::string::npos))
            result.entries.push_back(readEntry(item));
    };
    if (query.empty()) {
        fs::directory_iterator it(root, ec), end;
        while (!ec && it != end && !cancel) { collect(*it); it.increment(ec); }
    } else {
        // Keep each directory independent: an error in one subtree must not
        // discard the iterator state for all remaining siblings.
        std::vector<fs::path> pending{root};
        while (!pending.empty() && !cancel && !result.truncated) {
            const auto folder = pending.back();
            pending.pop_back();
            std::error_code folderError;
            fs::directory_iterator it(folder, folderError), end;
            while (!folderError && it != end && !cancel) {
                const auto item = *it;
                if (showHidden || !hidden(item.path())) {
                    collect(item);
                    std::error_code statusError;
                    const auto status = item.symlink_status(statusError);
                    if (!statusError && fs::is_directory(status))
                        pending.push_back(item.path());
                    if (statusError && result.error.empty()) result.error = statusError.message();
                }
                if (result.entries.size() >= 50000) { result.truncated = true; break; }
                it.increment(folderError);
            }
            if (folderError && result.error.empty()) result.error = folderError.message();
        }
    }
    if (ec) result.error = ec.message();
    return result;
}
// Never merge or overwrite a destination. Preserve links instead of following them.
inline void transfer(const fs::path& source, const fs::path& folder, bool move) {
    const auto target = folder / source.filename();
    if (source.filename().empty()) throw std::runtime_error("Cannot transfer a filesystem root.");
    if (!fs::is_directory(folder)) throw std::runtime_error("Destination folder is unavailable.");
    if (fs::exists(fs::symlink_status(target)))
        throw std::runtime_error("An item with that name already exists: " + target.u8string());
    if (fs::is_directory(source) && !fs::is_symlink(source)) {
        const auto canonicalSource = fs::canonical(source);
        auto ancestor = fs::canonical(folder);
        while (!ancestor.empty()) {
            if (fs::equivalent(canonicalSource, ancestor))
                throw std::runtime_error("Cannot paste a folder into itself or its subfolders.");
            const auto parent = ancestor.parent_path();
            if (parent == ancestor) break;
            ancestor = parent;
        }
    }
    if (move) {
        std::error_code ec;
        fs::rename(source, target, ec);
        if (!ec) return;
        if (ec != std::errc::cross_device_link) throw fs::filesystem_error("Move failed", source, target, ec);
    }
    // If copying fails, retain the source, including for cross-device moves.
    fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
    if (move) fs::remove_all(source);
}
struct DeletePlan {
    std::vector<fs::path> items; // Children follow parents; remove in reverse order.
    uintmax_t bytes = 0;
};
inline DeletePlan planDelete(const std::vector<fs::path>& selected,
                             const std::function<bool(size_t)>& progress = {}) {
    DeletePlan plan;
    std::set<fs::path> seen;
    auto add = [&](const fs::path& path) {
        auto absolute = fs::absolute(path).lexically_normal();
        if (absolute == absolute.root_path()) throw std::runtime_error("Cannot delete a filesystem root.");
        if (!seen.insert(absolute).second) return;
        auto status = fs::symlink_status(absolute);
        if (!fs::exists(status)) throw std::runtime_error("An item is no longer available: " + absolute.u8string());
        plan.items.push_back(absolute);
        if (fs::is_regular_file(status)) plan.bytes += fs::file_size(absolute);
        if (progress && !progress(plan.items.size())) throw std::runtime_error("Counting cancelled. Nothing was deleted.");
    };
    // Sort parents before children even when both are selected in search results.
    auto roots = selected;
    std::sort(roots.begin(), roots.end());
    for (const auto& path : roots) {
        if (seen.count(fs::absolute(path).lexically_normal())) continue;
        add(path);
        if (fs::is_directory(fs::symlink_status(path)))
            for (const auto& item : fs::recursive_directory_iterator(path)) add(item.path());
    }
    return plan;
}
inline std::string executeDelete(const DeletePlan& plan,
                                const std::function<bool(size_t)>& progress = {}) {
    size_t completed = 0;
    std::string errors;
    for (auto it = plan.items.rbegin(); it != plan.items.rend(); ++it) {
        if (progress && !progress(completed)) {
            errors += "Deletion stopped. Some items may already have been deleted.\n";
            break;
        }
        std::error_code ec;
        fs::remove(*it, ec); // Do not recursively remove items added after confirmation.
        if (ec) errors += it->u8string() + ": " + ec.message() + "\n";
        ++completed;
    }
    return errors;
}
inline bool validName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find_first_of("/\\") == std::string::npos;
}
} // namespace explorer
