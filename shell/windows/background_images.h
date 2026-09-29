#pragma once
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace explorer::background {
namespace fs = std::filesystem;
struct ImageFile {
    fs::path path;
    fs::file_time_type modified{};
    std::uintmax_t size{};
    bool operator==(const ImageFile& other) const {
        return path == other.path && modified == other.modified && size == other.size;
    }
};

inline bool supportedImage(const fs::path& path) {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) { return std::towlower(ch); });
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
}

// A deterministic choice independent of the filesystem's enumeration order.
inline ImageFile selectImage(const fs::path& folder) {
    ImageFile selected;
    if (folder.empty()) return selected;
    std::error_code ec;
    fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code fileError;
        if (!supportedImage(it->path()) || !it->is_regular_file(fileError) || fileError) continue;
        const auto modified = it->last_write_time(fileError);
        if (fileError) continue;
        const auto size = it->file_size(fileError);
        if (fileError) continue;
        if (selected.path.empty() || it->path().filename().wstring() < selected.path.filename().wstring())
            selected = {it->path(), modified, size};
    }
    return selected;
}
}
