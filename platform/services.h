#pragma once
#include <filesystem>

namespace explorer::platform {
// Implemented by exactly one platform adapter selected by CMake.
std::filesystem::path homeDirectory();
bool openFile(const std::filesystem::path& path);
}
