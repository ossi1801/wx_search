#include "shell/windows/background_images.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace explorer::background;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    const auto folder = fs::temp_directory_path() /
        ("rexplorer-background-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(selectImage(folder).path.empty(), "missing folder must use placeholder");
        require(selectImage({}).path.empty(), "unavailable background folder must not search current directory");
        fs::create_directories(folder);
        require(selectImage(folder).path.empty(), "empty folder must use placeholder");
        std::ofstream(folder / "notes.txt") << "not an image";
        fs::create_directories(folder / "0-directory.png");
        fs::create_directories(folder / "nested");
        std::ofstream(folder / "nested" / "0-nested.png") << "nested";
        require(selectImage(folder).path.empty(), "only top-level image files may be selected");
        std::ofstream(folder / "z-wallpaper.jpg") << "jpeg";
        std::ofstream(folder / "a-wallpaper.PNG") << "png";
        const auto selected = selectImage(folder);
        require(selected.path.filename() == "a-wallpaper.PNG", "selection must be alphabetical, with case-insensitive extensions");
        std::ofstream(selected.path, std::ios::app) << "replacement";
        require(!(selectImage(folder) == selected), "file replacement must invalidate cached image");
        const auto resized = selectImage(folder);
        fs::last_write_time(selected.path, resized.modified + std::chrono::seconds(2));
        require(!(selectImage(folder) == resized), "same-sized edits must invalidate by modification time");
        fs::remove(selected.path);
        require(selectImage(folder).path.filename() == "z-wallpaper.jpg", "removing selected image must choose next image");
        fs::remove(folder / "z-wallpaper.jpg");
        require(selectImage(folder).path.empty(), "removing all images must restore placeholder");
        for (const auto* name : {"a.png", "a.PNG", "a.jpg", "a.JPEG", "a.bmp"})
            require(supportedImage(name), "supported extension rejected");
        require(!supportedImage("a.gif") && !supportedImage("a.png.tmp"), "unsupported or partial image accepted");
        fs::remove_all(folder);
        std::cout << "Background selection and reload tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ec;
        fs::remove_all(folder, ec);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
