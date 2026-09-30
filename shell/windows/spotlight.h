#pragma once
#include <windows.h>
namespace explorer::spotlight {
bool create(HINSTANCE instance, HWND owner, unsigned dpi);
void toggle();
void destroy();
}
