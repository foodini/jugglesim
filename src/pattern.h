// pattern.h - the juggling pattern as throw events in ticks: the source of truth for juggling.
//
// The model is the general "sequence" one: throw events plus a list of sections, each a loop
// body with a repeat count. Pattern mode is the special case of a single section, starting at
// tick 0, that repeats forever. Sequence mode (not implemented yet) will edit the general case.
//
// Siteswap text is one way to generate a pattern (patternFromSiteswap) and one way to display
// it (patternToSiteswap) when it's expressible. The ladder reads the pattern, not the text.
#pragma once

#include "siteswap.h"
#include "timing.h"

#include <string>
#include <vector>

enum class Hand { Right, Left };

inline Hand otherHand(Hand h) { return h == Hand::Right ? Hand::Left : Hand::Right; }

struct ThrowEvent {
    int juggler = 0;
    Hand hand = Hand::Right;
    Tick throwTick = 0;
    int value = 0;  // siteswap value, in beats
    int destJuggler = 0;
    Hand destHand = Hand::Right;
    int spins = 0;  // full rotations of a club in flight

    Tick catchTick() const { return throwTick + beatsToTicks(value); }
};

constexpr int kRepeatForever = -1;

// A section's body is stored at its PHYSICAL period: long enough that the throws and the
// hands throwing them both repeat. Async juggling alternates hands every beat, so a loop with
// an odd number of beats (531) only comes back to the same hand after two passes
// (R L R L R L), and is stored as 6 beats. Every event therefore has its literal hand, and
// nothing that reads events needs to know about odd-length loops.
//
// loopBeats is the length of the loop as the user works with it (3 for 531), used for the
// siteswap text and the period control. length is always a multiple of it.
struct Section {
    Tick start = 0;
    Tick length = 0;               // physical period of the body, in ticks
    int loopBeats = 0;             // loop length as written/edited, in beats
    int repeats = kRepeatForever;  // passes through the body, or kRepeatForever
};

struct Pattern {
    std::vector<ThrowEvent> events;  // sorted by throwTick; each lies inside its section body
    std::vector<Section> sections;
};

// Default spin count for a throw value: 1 = no spin (handed across), 2 = no spin (held),
// 3 = single, 4-5 = double, 6-7 = triple, and so on.
int defaultSpinCount(int value);

// --- Pattern mode (one section, repeating forever, one juggler, async) ---

// Builds a pattern-mode pattern from a valid vanilla siteswap. Beat 0 is thrown by the right
// hand. 0s become empty beats (no event).
Pattern patternFromSiteswap(const Siteswap& siteswap);

// Length of the loop in beats, as the user works with it (3 for 531, not its stored 6).
int loopPeriodBeats(const Pattern& pattern);

// Throw value on each beat of the loop body (0 where there's no throw). Size = loop period.
std::vector<int> loopThrowValues(const Pattern& pattern);

int ballCount(const Pattern& pattern);

// Shortest period (in beats) the loop actually repeats at: 531531 -> 3, 333 -> 1.
int shortestPeriodBeats(const Pattern& pattern);

// Same juggling, with the loop body written out at a different length. periodBeats must be a
// multiple of shortestPeriodBeats(pattern); 531 at period 6 is 531531.
Pattern withPeriod(const Pattern& pattern, int periodBeats);

// Siteswap text for the loop, written at its current period ("531531" stays "531531").
// Returns false if a value can't be written as a single siteswap character (36 and up, or
// 25 and 33, whose letters 'p' and 'x' are reserved for passing and sync).
bool patternToSiteswap(const Pattern& pattern, std::string* text);
