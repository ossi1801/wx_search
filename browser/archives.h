#pragma once
#include <filesystem>
#include <functional>
#include <vector>

namespace explorer {
// Outputs must not exist. Failed or cancelled operations remove their partial output.
using ArchiveProgress = std::function<bool(const std::filesystem::path&)>;
void createZip(const std::vector<std::filesystem::path>& sources,
               const std::filesystem::path& output, ArchiveProgress progress = {});
void extractZip(const std::filesystem::path& source,
                const std::filesystem::path& destination, ArchiveProgress progress = {});
}
