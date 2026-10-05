// siteswap.cpp - see siteswap.h.
#include "siteswap.h"

#include <cctype>

int Siteswap::throwAt(int beat) const {
    int n = period();
    if (n == 0) return 0;
    int i = beat % n;
    if (i < 0) i += n;
    return throws[static_cast<size_t>(i)];
}

Siteswap parseSiteswap(const std::string& text) {
    Siteswap s;
    for (char raw : text) {
        unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isspace(ch)) continue;
        if (ch >= '0' && ch <= '9') {
            s.throws.push_back(ch - '0');
        } else if (std::isalpha(ch) && std::tolower(ch) != 'x' && std::tolower(ch) != 'p') {
            s.throws.push_back(std::tolower(ch) - 'a' + 10);
        } else if (ch == '(' || ch == ')' || ch == ',' || ch == '*') {
            s.error = "Synchronous siteswap isn't supported yet.";
            s.throws.clear();
            return s;
        } else if (ch == '[' || ch == ']') {
            s.error = "Multiplex siteswap isn't supported yet.";
            s.throws.clear();
            return s;
        } else if (ch == '<' || ch == '>' || ch == '|' || std::tolower(ch) == 'p') {
            s.error = "Passing siteswap isn't supported yet.";
            s.throws.clear();
            return s;
        } else if (std::tolower(ch) == 'x') {
            s.error = "'x' only applies to synchronous siteswap, which isn't supported yet.";
            s.throws.clear();
            return s;
        } else {
            s.error = std::string("Unexpected character '") + raw + "'.";
            s.throws.clear();
            return s;
        }
    }

    const int n = s.period();
    if (n == 0) return s;  // blank input: not valid, but no error to show

    int sum = 0;
    for (int t : s.throws) sum += t;
    if (sum % n != 0) {
        s.error = "Invalid: the average throw value (" + std::to_string(sum) + "/" +
                  std::to_string(n) + ") isn't a whole number.";
        return s;
    }

    // Each beat in the period must receive exactly one landing.
    std::vector<int> landedFrom(static_cast<size_t>(n), -1);
    for (int i = 0; i < n; ++i) {
        int landing = (i + s.throws[static_cast<size_t>(i)]) % n;
        if (landedFrom[static_cast<size_t>(landing)] >= 0) {
            s.error = "Invalid: throws on beats " + std::to_string(landedFrom[landing] + 1) +
                      " and " + std::to_string(i + 1) + " land on the same beat.";
            return s;
        }
        landedFrom[static_cast<size_t>(landing)] = i;
    }

    s.ballCount = sum / n;
    s.valid = true;
    return s;
}
