#include "shell/windows/taskbar_model.h"
#include <iostream>
#include <stdexcept>
using namespace explorer::taskbar;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        WindowTraits normal;
        require(eligible(normal), "ordinary application must appear");
        auto dialog = normal; dialog.owned = true;
        require(!eligible(dialog), "owned dialog must not duplicate application");
        dialog.appWindow = true;
        require(eligible(dialog), "explicit app-window dialog must appear");
        auto hiddenDesktop = normal; hiddenDesktop.cloaked = true;
        require(!eligible(hiddenDesktop), "cloaked window must not appear");
        auto tool = normal; tool.tool = true; tool.appWindow = true;
        require(!eligible(tool), "tool window must be excluded");
        auto hidden = normal; hidden.visible = false;
        require(!eligible(hidden), "invisible window must be excluded");
        auto own = normal; own.excluded = true;
        require(!eligible(own), "shell's own windows must be excluded");
        for (int width : {0, 100, 424, 425, 640, 1920, 3840}) {
            for (unsigned dpi : {96u, 144u, 192u}) {
                for (size_t count : {0u, 1u, 10u, 1000u}) {
                    auto l = layout(width, dpi, count);
                    require(l.count >= 0 && static_cast<size_t>(l.count) <= count, "invalid visible count");
                    require(l.taskLeft() + l.count * l.taskWidth + l.more + l.network + l.sound + l.clock <= width,
                        "task buttons overlap system controls");
                    if (l.count) require(l.taskWidth >= static_cast<int>(90 * dpi / 96), "task buttons too narrow");
                }
            }
        }
        require(layout(640, 96, 100).count < 100, "crowded taskbar must use overflow");
        std::cout << "Taskbar policy and layout tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
