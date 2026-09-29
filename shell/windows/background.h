#pragma once
#include <windows.h>
#include <filesystem>

namespace explorer::background {
std::filesystem::path folder();
bool create(HINSTANCE instance);
void refresh();
void toggle();
void destroy();
bool visible();
HWND handle();
}
