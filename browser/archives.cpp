#include "archives.h"
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <cctype>

namespace explorer {
namespace {
namespace fs = std::filesystem;
wxString stringOf(const fs::path& path) { return wxString::FromUTF8(path.u8string()); }
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void report(const ArchiveProgress& progress, const fs::path& path) {
    require(!progress || progress(path), "Archive operation cancelled.");
}
fs::path safeEntry(const wxString& value) {
    std::string name = value.ToUTF8().data();
    std::replace(name.begin(), name.end(), '\\', '/');
    require(!name.empty() && name.front() != '/' && name.find(':') == std::string::npos &&
            name.find('\0') == std::string::npos, "Unsafe path in ZIP archive.");
    fs::path result;
    size_t start = 0;
    while (start < name.size()) {
        auto end = name.find('/', start);
        auto part = name.substr(start, end == std::string::npos ? end : end - start);
        require(!part.empty() && part != "." && part != ".." &&
                part.back() != '.' && part.back() != ' ' &&
                part.find_first_of("<>\"|?*") == std::string::npos &&
                std::none_of(part.begin(), part.end(), [](unsigned char c) { return c < 32; }),
                "Unsafe path in ZIP archive.");
        auto base = part.substr(0, part.find('.'));
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return std::toupper(c); });
        require(base != "CON" && base != "PRN" && base != "AUX" && base != "NUL" &&
                !(base.size() == 4 && (base.substr(0, 3) == "COM" || base.substr(0, 3) == "LPT") &&
                  base[3] >= '1' && base[3] <= '9'), "Reserved Windows path in ZIP archive.");
        result /= fs::u8path(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
}

void createZip(const std::vector<fs::path>& sources, const fs::path& output, ArchiveProgress progress) {
    require(!sources.empty(), "Select files or folders to compress.");
    require(!fs::exists(output), "The output already exists. Choose a new archive name.");
    // Gather first: reject links and collisions, and never include our own output.
    std::vector<std::pair<fs::path, fs::path>> items;
    std::set<fs::path> names;
    for (const auto& source : sources) {
        const auto root = source.filename();
        require(!root.empty() && names.insert(root).second, "Selected items have duplicate names.");
        auto add = [&](const fs::path& path) {
            report(progress, path);
            auto status = fs::symlink_status(path);
            require(fs::is_directory(status) || fs::is_regular_file(status), "ZIP compression supports regular files and folders only; links are not followed.");
            require(fs::absolute(path).lexically_normal() != fs::absolute(output).lexically_normal(), "The archive cannot be inside a selected folder.");
            items.emplace_back(path, path == source ? root : root / path.lexically_relative(source));
        };
        if (fs::is_directory(fs::symlink_status(source))) {
            auto relative = fs::absolute(output).lexically_normal().lexically_relative(fs::absolute(source).lexically_normal());
            require(relative.empty() || *relative.begin() == "..", "The archive cannot be inside a selected folder.");
        }
        add(source);
        if (fs::is_directory(source)) for (const auto& item : fs::recursive_directory_iterator(source)) add(item.path());
    }
    try {
        wxFFileOutputStream file(stringOf(output));
        require(file.IsOk(), "Cannot create ZIP archive.");
        wxZipOutputStream zip(file);
        for (const auto& item : items) {
            report(progress, item.first);
            auto name = item.second.lexically_normal().generic_u8string();
            if (fs::is_directory(item.first)) require(zip.PutNextDirEntry(wxString::FromUTF8(name)), "Cannot write ZIP folder.");
            else {
                require(zip.PutNextEntry(wxString::FromUTF8(name)), "Cannot write ZIP entry.");
                wxFFileInputStream input(stringOf(item.first));
                require(input.IsOk(), "Cannot read selected file.");
                zip.Write(input);
                require(input.GetLastError() == wxSTREAM_EOF && zip.IsOk(), "Failed to copy file into ZIP archive.");
            }
            require(zip.CloseEntry(), "Cannot finish ZIP entry.");
        }
        require(zip.Close() && file.Close(), "Cannot finish ZIP archive.");
    } catch (...) { std::error_code ec; fs::remove(output, ec); throw; }
}

void extractZip(const fs::path& source, const fs::path& destination, ArchiveProgress progress) {
    require(!fs::exists(destination), "The destination already exists. Choose a new folder.");
    wxFFileInputStream file(stringOf(source));
    require(file.IsOk(), "Cannot read ZIP archive.");
    wxZipInputStream zip(file);
    require(fs::create_directory(destination), "Cannot create destination folder.");
    try {
        while (std::unique_ptr<wxZipEntry> entry{zip.GetNextEntry()}) {
            auto relative = safeEntry(entry->GetName());
            report(progress, relative);
            const auto target = destination / relative;
            if (entry->IsDir()) fs::create_directories(target);
            else {
                require(!fs::exists(target), "Duplicate file in ZIP archive.");
                fs::create_directories(target.parent_path());
                wxFFileOutputStream output(stringOf(target));
                require(output.IsOk(), "Cannot create extracted file.");
                zip.Read(output);
                require(zip.GetLastError() == wxSTREAM_EOF && output.IsOk() && output.Close(), "Invalid or unsupported ZIP entry.");
            }
            require(zip.CloseEntry(), "Invalid ZIP entry.");
        }
        require(zip.GetLastError() == wxSTREAM_EOF, "Invalid or unsupported ZIP archive.");
    } catch (...) { std::error_code ec; fs::remove_all(destination, ec); throw; }
}
}
