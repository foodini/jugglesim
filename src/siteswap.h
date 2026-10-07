// siteswap.h - siteswap parsing and validation.
//
// Current scope: vanilla siteswap and two-juggler asynchronous passing (Juggling Lab's
// "<3p 3|3p 3>"). Throw values 0-9 and a-z (a = 10 ... z = 35), except that 'p' and 'x' are
// reserved for the passing and sync extensions rather than meaning 25 and 33. Whitespace is
// ignored. Sync "( , )", multiplex "[ ]" and passing with 3+ jugglers are recognized and
// reported as not-yet-supported, so the user gets a clear message rather than a parse error.
#pragma once

#include <string>
#include <vector>

// One throw in a loop of throws: its value (beats until the prop is thrown again) and which
// juggler throws it next (the same juggler for a self, another for a pass).
struct LoopThrow {
    int value = 0;
    int dest = 0;  // 0-based juggler number
    bool operator==(const LoopThrow& o) const { return value == o.value && dest == o.dest; }
    bool operator!=(const LoopThrow& o) const { return !(*this == o); }
};

// The repeating loop of one or more jugglers, all throwing on every beat (async), each starting
// with the right hand. throws[juggler * period + beat].
struct JugglingLoop {
    int jugglers = 0;
    int period = 0;
    std::vector<LoopThrow> throws;

    bool empty() const { return period == 0 || jugglers == 0; }
    // The throw of `juggler` on any beat (negative and past-the-period beats wrap around).
    const LoopThrow& at(int juggler, int beat) const {
        int b = beat % period;
        if (b < 0) b += period;
        return throws[static_cast<size_t>(juggler * period + b)];
    }
    bool operator==(const JugglingLoop& o) const {
        return jugglers == o.jugglers && period == o.period && throws == o.throws;
    }
    bool operator!=(const JugglingLoop& o) const { return !(*this == o); }
};

// A single juggler's loop from plain throw values.
JugglingLoop soloLoop(const std::vector<int>& values);

// Number of props in a valid loop (the sum of all throws over the period).
int loopPropCount(const JugglingLoop& loop);

struct Siteswap {
    std::vector<int> throws;  // juggler 1's throw values, one per beat (the whole pattern solo)
    JugglingLoop loop;        // every juggler's throws (also filled in for a solo pattern)
    int ballCount = 0;
    bool valid = false;
    std::string error;  // empty when valid, or when the input was blank

    int period() const { return static_cast<int>(throws.size()); }
    int jugglers() const { return loop.jugglers; }
    // Throw value at any beat (handles negative beats and beats past one period).
    int throwAt(int beat) const;
};

// Parses vanilla siteswap ("531") or Juggling Lab's asynchronous passing notation for two
// jugglers ("<3p 3|3p 3>": one field per juggler, 'p' marks a pass to the other juggler).
// Passing patterns with three or more jugglers are recognized and reported as not supported
// yet, as are sync and multiplex notation.
Siteswap parseSiteswap(const std::string& text);
