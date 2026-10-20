// test_data.h - shared helpers for the tests: the built-in pattern library (data/patterns.txt,
// read from the source tree), as text and parsed.
#pragma once

#include "pattern_library.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// data/patterns.txt, found from the working directory (the repository root, or a folder or
// two below it, as the build runs the tests from). Empty if it can't be found.
inline std::string libraryText() {
    for (const char* path : {"data/patterns.txt", "../data/patterns.txt", "../../data/patterns.txt"}) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }
    return std::string();
}
inline const std::vector<LibraryPattern>& library() {
    static const std::vector<LibraryPattern> patterns = parsePatternLibrary(libraryText());
    return patterns;
}
