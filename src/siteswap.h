// siteswap.h - siteswap parsing and validation.
//
// Current scope: vanilla siteswap and asynchronous passing for 2 to 6 jugglers (Juggling Lab's
// "<3p 3|3p 3>", "<3p2 3|3p3 3|3p1 3>"), plus relative pass targets ("3p+1": the next juggler)
// and "?" for a throw not decided yet (a sketch). Throw values 0-9 and a-z (a = 10 ... z = 35),
// except that 'p' and 'x' are reserved for the passing and sync extensions rather than meaning
// 25 and 33. Whitespace is ignored. Sync "( , )" and multiplex "[ ]" are recognized and
// reported as not-yet-supported, so the user gets a clear message rather than a parse error.
#pragma once

#include <string>
#include <vector>

// The most jugglers a pattern can have.
constexpr int kMaxJugglers = 6;

// The value of a throw that hasn't been decided yet, in a sketch ("?" in siteswap text).
constexpr int kOpenThrow = -1;

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

// How a passing pattern's passes were written, so text made from the pattern again (after an
// edit on the ladder) is written the same way. One style per pattern: if any target was
// relative ("3p+1"), every pass is written relative; otherwise passes are written as Juggling
// Lab does (a bare "p" for two jugglers, "3p2" for more). A self written as a pass to yourself
// ("3p+0", kept to line up columns in a file) stays written that way while it's still a self.
struct PassStyle {
    bool relative = false;
    int jugglers = 0, period = 0;     // the loop selfAsPass goes with
    std::vector<bool> selfAsPass;     // [juggler * period + beat]
    bool marked(int juggler, int beat, int loopJugglers, int loopPeriod) const {
        return loopJugglers == jugglers && loopPeriod == period && juggler < jugglers && beat < period &&
               selfAsPass[static_cast<size_t>(juggler * period + beat)];
    }
};

struct Siteswap {
    std::vector<int> throws;  // juggler 1's throw values, one per beat (the whole pattern solo)
    JugglingLoop loop;        // every juggler's throws (also filled in for a solo pattern)
    int ballCount = 0;
    bool valid = false;
    // A sketch: some throws are "?" (open), and no two decided throws land in the same spot.
    // Not valid (it can't be juggled yet), but not an error either; loop holds it.
    bool sketch = false;
    int openThrows = 0;
    std::string error;  // empty when valid, for a sketch, or when the input was blank
    // The throws the error is about, when it's about particular throws (two that land together),
    // so the text box can underline them: {juggler, beat in the loop}.
    struct ThrowRef {
        int juggler = 0;
        int beat = 0;
    };
    std::vector<ThrowRef> problemThrows;
    PassStyle passStyle;  // how the passes were written (passing patterns)

    int period() const { return static_cast<int>(throws.size()); }
    int jugglers() const { return loop.jugglers; }
    // Throw value at any beat (handles negative beats and beats past one period).
    int throwAt(int beat) const;
};

// Parses vanilla siteswap ("531") or Juggling Lab's asynchronous passing notation ("<3p 3|3p 3>":
// one field per juggler, 'p' marks a pass). With two jugglers a bare 'p' passes to the other one;
// with three or more, the target follows the 'p': its number ("3p2", as Juggling Lab writes it)
// or, our extension (as passist.org writes them), how many jugglers along ("3p+1" is the next
// juggler, "3p-1" the one before, wrapping around; "3p+0" is a self). "?" is an open throw
// (JuggleSim's own extension, for sketches). Up to kMaxJugglers jugglers. Sync and multiplex
// notation are reported as not supported yet.
Siteswap parseSiteswap(const std::string& text);
