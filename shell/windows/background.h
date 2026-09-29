#pragma once
#include <windows.h>

namespace explorer::background {
bool create(HINSTANCE instance);
void refresh();
void toggle();
void destroy();
bool visible();
HWND handle();
}
