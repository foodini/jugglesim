// ladder_edit.h - the "siteswap" edit chain behind ladder drag-editing (Pattern mode).
//
// The user picks up one end of a throw. Picking up an ARRIVAL leaves its landing spot empty
// (the "hole"); dropping the held arrival on another landing spot displaces whatever landed
// there, which is picked up in turn. Picking up a DEPARTURE works the same way with the beats
// throws leave from. The chain closes when the held end is dropped on the hole.
//
// While the chain is open, edits are LOCAL: only the throws actually touched change, on the
// pattern laid out along the timeline. When the chain closes, the edit becomes a loop: its
// length is the stretch of beats the chain touched, rounded up to a multiple of the current
// loop period (so the period control acts as a minimum). In a "3", touching beats 1-3 to make
// 5, 3, 1 gives the loop 531.
//
// Closing on the hole's copy in another repeat (a whole number of periods away) is allowed when
// asked for explicitly (the UI uses Shift-click), if some loop length fits: it must be at least
// the stretch touched and divide the distance to the hole. The shortest such length is used.
// That makes one throw a whole loop higher or lower, adding or removing balls (3 can become 9
// this way; unroll to period 3 first to get 933). Without the request, a drop on a copy just
// picks up the throw that lands (or starts) there, like any other drop. (In a 1-beat loop such
// as "3", every beat is a copy of the hole, so this has to be opt-in.)
//
// Every drop exchanges where two throws land (or start), and the loop is never shorter than the
// stretch touched, so a closed chain always yields a valid siteswap. Empty beats (0s) take part
// as throws that land on their own beat.
//
// Beats are absolute beats on the ladder (0 = the first beat drawn) and may be negative.
// Nothing here draws or reads input; ladder_view does that and calls these.
#pragma once

#include <map>
#include <vector>

enum class HeldEnd { Arrival, Departure };

constexpr int kNoThrow = -1;  // a departure whose throw is currently held / missing

// Longest loop an edit may produce (and the period control may write out), in beats.
constexpr int kMaxLoopBeats = 64;

struct EditChain {
    bool active = false;
    HeldEnd end = HeldEnd::Arrival;
    int period = 0;               // the loop's period when the chain started
    std::vector<int> base;        // the loop when the chain started
    std::map<int, int> overrides; // departure beat -> new value (kNoThrow while held/missing)

    // The held throw. For a held ARRIVAL, fixedBeat is the beat it's thrown on (an override
    // set to kNoThrow until it's placed). For a held DEPARTURE, fixedBeat is the beat it lands
    // on, and the hole is the departure beat it left empty.
    int fixedBeat = 0;
    int heldValue = 0;  // value the held throw had when it was picked up (0 = an empty beat)

    int holeBeat = 0;   // the empty spot: an arrival beat or a departure beat
};

int positiveMod(int a, int m);

// Throw value on an absolute beat, with the chain's edits applied (kNoThrow if held).
int editValueAt(const EditChain& chain, int beat);

// Starts a chain by picking up one end of the throw made on `throwBeat`.
EditChain beginEditChain(const std::vector<int>& loop, int throwBeat, HeldEnd end);

// Value the held throw would get if dropped on `targetBeat` (an arrival beat for a held
// arrival, a departure beat for a held departure). Returns false if that isn't a legal drop:
// values must be 0-35, excluding 25 and 33 (their letters, p and x, mean passing and sync in
// siteswap notation, so they can't be written).
bool editDropValue(const EditChain& chain, int targetBeat, int* value);

// If dropping on targetBeat would close the chain, returns the resulting loop length;
// otherwise 0 (the drop, if legal, displaces whatever is there instead). closeOnCopy allows
// closing on the hole's copy in another repeat. Closes that would make a loop longer than
// kMaxLoopBeats are refused (0); editCloseLoopLengthUnlimited says how long it would have been.
int editCloseLoopLength(const EditChain& chain, int targetBeat, bool closeOnCopy);
int editCloseLoopLengthUnlimited(const EditChain& chain, int targetBeat, bool closeOnCopy);

// Ball-count change closing on targetBeat would make (0 on the hole itself), assuming
// closeOnCopy. Only meaningful when editCloseLoopLength(chain, targetBeat, true) > 0.
int editDropBallDelta(const EditChain& chain, int targetBeat);

// Loop length the edit would get if it closed with what's been touched so far.
int editProvisionalLoopLength(const EditChain& chain);

enum class DropResult { Invalid, Continued, Closed };

// Drops the held end on targetBeat. On Closed, *resultLoop receives the new loop and the
// chain is no longer active.
DropResult editDrop(EditChain& chain, int targetBeat, bool closeOnCopy,
                    std::vector<int>* resultLoop);
