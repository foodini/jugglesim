// siteswap.h - siteswap parsing and validation.
//
// Current scope: vanilla (asynchronous, single-juggler, non-multiplex) siteswap only.
// Throw values 0-9 and a-z (a = 10 ... z = 35), except that 'p' and 'x' are reserved for the
// passing and sync extensions rather than meaning 25 and 33. Whitespace is ignored.
// Sync "( , )", multiplex "[ ]", and passing "< | >" are recognized and reported as
// not-yet-supported so the user gets a clear message rather than a generic parse error.
#pragma once

#include <string>
#include <vector>

struct Siteswap {
    std::vector<int> throws;  // one entry per beat, repeating with period throws.size()
    int ballCount = 0;
    bool valid = false;
    std::string error;  // empty when valid, or when the input was blank

    int period() const { return static_cast<int>(throws.size()); }
    // Throw value at any beat (handles negative beats and beats past one period).
    int throwAt(int beat) const;
};

Siteswap parseSiteswap(const std::string& text);
