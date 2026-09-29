#pragma once
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

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
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end && !cancel) {
            if (!showHidden && hidden(it->path())) it.disable_recursion_pending();
            else collect(*it);
            if (result.entries.size() >= 50000) { result.truncated = true; break; }
            it.increment(ec);
        }
    }
    if (ec) result.error = ec.message();
    return result;
}
inline bool validName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find_first_of("/\\") == std::string::npos;
}
} // namespace explorer
