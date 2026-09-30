#pragma once
#include <string>
#include <cwctype>
namespace explorer::search {
inline std::wstring normalize(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}
// Every word must match; prioritize exact names, then prefixes, then substrings.
inline int score(const std::wstring& name, const std::wstring& path, const std::wstring& query, bool app) {
    if (query.empty()) return app ? 20 : 0;
    int result = app ? 20 : 0;
    size_t start = 0;
    while (start < query.size()) {
        start = query.find_first_not_of(L" \t", start);
        if (start == std::wstring::npos) break;
        auto end = query.find_first_of(L" \t", start);
        auto word = query.substr(start, end == std::wstring::npos ? end : end - start);
        auto pos = name.find(word);
        if (pos != std::wstring::npos) result += pos == 0 ? 100 : 60;
        else if (path.find(word) != std::wstring::npos) result += 10;
        else return -1;
        if (end == std::wstring::npos) break;
        start = end;
    }
    if (name == query) result += 200;
    return result;
}
}
