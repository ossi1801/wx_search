#pragma once
#include <algorithm>
#include <cstddef>

namespace explorer::taskbar {
// Platform-neutral policy; OS queries stay in the Windows adapter.
struct WindowTraits {
    bool visible = true, excluded = false, tool = false, noActivate = false;
    bool owned = false, appWindow = false, cloaked = false, titled = true;
};
inline bool eligible(const WindowTraits& value) {
    return value.visible && !value.excluded && !value.tool && !value.noActivate &&
        (!value.owned || value.appWindow) && !value.cloaked && value.titled;
}
struct Layout {
    int start, files, more, clock, taskWidth, count;
    int taskLeft() const { return start + files; }
};
inline Layout layout(int width, unsigned dpi, size_t tasks) {
    width = std::max(0, width);
    auto scale = [dpi](int x) { return std::max(1, static_cast<int>(x * std::max(96u, dpi) / 96)); };
    Layout result{scale(70), scale(70), scale(50), scale(80), 0, 0};
    const int fixed = result.start + result.files + result.more + result.clock;
    if (width < fixed) {
        result.start = width * 70 / 270;
        result.files = width * 70 / 270;
        result.more = width * 50 / 270;
        result.clock = width - result.start - result.files - result.more;
        return result;
    }
    const int available = width - fixed;
    result.count = static_cast<int>(std::min(tasks, static_cast<size_t>(available / scale(90))));
    if (result.count) result.taskWidth = std::min(scale(180), available / result.count);
    return result;
}
}
