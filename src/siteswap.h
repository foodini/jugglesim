// siteswap.h - siteswap parsing and validation.
//
// Current scope: vanilla siteswap and asynchronous passing for 2 to 6 jugglers (Juggling Lab's
// "<3p 3|3p 3>", "<3p2 3|3p3 3|3p1 3>"), plus relative pass targets ("3p+1": the next juggler),
// links between jugglers ("@1+3"), swapped hands (",LRswap") and "?" for a throw not decided yet
// (a sketch). Throw values 0-9 and a-z (a = 10 ... z = 35),
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

// The repeating loop of one or more jugglers, all throwing on every beat (async), the right hand
// on beat 1 (beat 0 here) and every other beat, unless the juggler's hands are swapped.
// throws[juggler * period + beat].
struct JugglingLoop {
    int jugglers = 0;
    int period = 0;
    std::vector<LoopThrow> throws;
    // Jugglers whose hands are swapped ("LRswap"): their left hand throws on beat 1. Indexed by
    // juggler; missing entries (or an empty vector) mean not swapped.
    std::vector<char> swapHands;

    bool handsSwapped(int juggler) const {
        return juggler >= 0 && juggler < static_cast<int>(swapHands.size()) && swapHands[static_cast<size_t>(juggler)] != 0;
    }
    // Whether `juggler` throws with the right hand on `beat` (any beat, negative too).
    bool rightHandBeat(int juggler, int beat) const { return (((beat % 2) + 2) % 2 == 0) != handsSwapped(juggler); }
    bool anyHandsSwapped() const {
        for (int j = 0; j < jugglers; ++j)
            if (handsSwapped(j)) return true;
        return false;
    }

    bool empty() const { return period == 0 || jugglers == 0; }
    // The throw of `juggler` on any beat (negative and past-the-period beats wrap around).
    const LoopThrow& at(int juggler, int beat) const {
        int b = beat % period;
        if (b < 0) b += period;
        return throws[static_cast<size_t>(juggler * period + b)];
    }
    bool operator==(const JugglingLoop& o) const {
        if (jugglers != o.jugglers || period != o.period || throws != o.throws) return false;
        for (int j = 0; j < jugglers; ++j)
            if (handsSwapped(j) != o.handsSwapped(j)) return false;
        return true;
    }
    bool operator!=(const JugglingLoop& o) const { return !(*this == o); }
};

// A single juggler's loop from plain throw values.
JugglingLoop soloLoop(const std::vector<int>& values);

// Number of props in a valid loop (the sum of all throws over the period).
int loopPropCount(const JugglingLoop& loop);

// How a pattern was written, so the text made from it again after an edit on the ladder is
// written the same way: each throw's pass target as typed, and which jugglers are links.
//
// Targets: a throw unchanged since it was typed is written as it was ("3p+1", "3p-4", "3p2",
// "3p", or "3p+0" for a self written as a pass to yourself, kept to line up columns in a file).
// A new or changed pass is written in the pattern's style: relative if the pattern has any
// relative targets (with "-" if they were all written with "-"), else as Juggling Lab does ("3p2",
// or a bare "p" with two jugglers).
//
// Links: "@2+3" is a juggler who does what J2 does, 3 beats later ("-3": earlier). A copy
// keeps each throw's target form, so a relative target shifts with it ("+1" is the copy's next
// juggler) and an absolute one stays put ("3p1" still goes to J1). A link copies the source's
// hands too; ",LRswap" after a part swaps that juggler's hands relative to what they'd be (for a
// plain part, the left hand throws on beat 1). An odd offset swaps hands by itself, since every
// throw moves to the other hand's beat.
struct ThrowForm {
    enum class Kind : unsigned char {
        Plain,     // a self, no "p"
        BareP,     // "p" with two jugglers: the other one
        Relative,  // "p+k" / "p-k": value is k as written (may be 0, negative, or past n)
        Absolute,  // "p2": value is the target juggler (0-based)
    };
    Kind kind = Kind::Plain;
    int value = 0;
};
struct PartLink {
    int to = -1;      // the juggler this one copies (0-based), or -1 for a part written out
    int offset = 0;   // beats later (negative: earlier)
    bool lrSwap = false;  // ",LRswap": hands swapped relative to the juggler copied
};
struct PatternForm {
    int jugglers = 0, period = 0;
    JugglingLoop original;          // the loop as typed (links resolved)
    std::vector<ThrowForm> throws;  // [juggler * period + beat], copies as their source's
    std::vector<PartLink> links;    // per juggler
    bool relative = false;          // the pattern has relative targets
    bool negative = false;          // ... all written with "-"

    bool hasLinks() const;
    bool linked(int juggler) const { return juggler >= 0 && juggler < static_cast<int>(links.size()) && links[static_cast<size_t>(juggler)].to >= 0; }
    // How throw t, made by `juggler` on `beat` of a loop shaped like this one (or written out at
    // a multiple of its period), is to be written: as typed if it's unchanged, else in the
    // pattern's style.
    ThrowForm formFor(int juggler, int beat, const LoopThrow& t) const;
    // Follows links from `juggler` to the part written out that it copies (itself if it isn't a
    // link), adding up the offsets. False for a loop of links (not possible in a parsed form).
    bool root(int juggler, int* rootJuggler, int* offset) const;
};

// The throw `form` describes for juggler `to`, given the throw t it describes for juggler `from`
// (the same value; a relative target shifted by to - from, an absolute one kept, a self kept a
// self).
LoopThrow carryThrow(const ThrowForm& form, const LoopThrow& t, int from, int to, int jugglers);

// What's wrong with a loop (two throws landing together, props not adding up), or "" if it's a
// valid pattern or a sketch.
std::string loopProblem(const JugglingLoop& loop);

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
    PatternForm form;  // how it was written: targets, links (passing patterns)

    int period() const { return static_cast<int>(throws.size()); }
    int jugglers() const { return loop.jugglers; }
    // Throw value at any beat (handles negative beats and beats past one period).
    int throwAt(int beat) const;
};

// Parses vanilla siteswap ("531") or Juggling Lab's asynchronous passing notation ("<3p 3|3p 3>":
// one field per juggler, 'p' marks a pass). With two jugglers a bare 'p' passes to the other one;
// with three or more, the target follows the 'p': its number ("3p2", as Juggling Lab writes it)
// or, our extension (as passist.org writes them), how many jugglers along ("3p+1" is the next
// juggler, "3p-1" the one before, wrapping around; "3p+0" is a self). A part may instead be a
// link ("@2+3", see PatternForm), and any part may end with options after commas (",LRswap"; a
// solo pattern too: "531,LRswap"). "?" is an open throw (JuggleSim's own extension, for
// sketches). Up to kMaxJugglers jugglers. Sync and multiplex notation are reported as not
// supported yet.
Siteswap parseSiteswap(const std::string& text);
