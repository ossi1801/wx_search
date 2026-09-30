#include "shell/windows/search_model.h"
#include <iostream>
#include <stdexcept>
using namespace explorer::search;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        require(normalize(L"NotePAD") == L"notepad", "case-insensitive normalization");
        require(score(L"notes", L"c:/notes", L"notes", false) > score(L"notes draft", L"c:/notes draft", L"notes", false), "exact beats prefix");
        require(score(L"notes draft", L"", L"notes", false) > score(L"draft notes", L"", L"notes", false), "prefix beats substring");
        require(score(L"draft notes", L"", L"notes", false) > score(L"draft", L"c:/notes/draft", L"notes", false), "name beats path");
        require(score(L"report.pdf", L"c:/work/report.pdf", L"work report", false) >= 0, "tokens can match name and folder");
        require(score(L"report.pdf", L"c:/work/report.pdf", L"work missing", false) == -1, "all tokens must match");
        require(score(L"editor", L"", L"editor", true) > score(L"editor", L"", L"editor", false), "apps win equivalent matches");
        require(score(L"editor", L"", L"", true) > score(L"editor", L"", L"", false), "empty query favors apps");
        std::cout << "Search ranking tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
