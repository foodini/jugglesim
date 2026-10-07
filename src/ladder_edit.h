// ladder_edit.h - the "siteswap" edit chain behind ladder drag-editing (Pattern mode).
//
// The user picks up one end of a throw. Picking up an ARRIVAL leaves its landing spot empty
// (the "hole"); dropping the held arrival on another landing spot displaces whatever landed
// there, which is picked up in turn. Picking up a DEPARTURE works the same way with the spots
// throws leave from. The chain closes when the held end is dropped on the hole.
//
// A spot is a juggler's hand on a beat (a Slot). With several jugglers a throw can be dropped
// on any of them: landing on another juggler makes it a pass. Where it's dropped decides
// everything: the value is the beat difference, and the juggler is the one dropped on.
//
// While the chain is open, edits are LOCAL: only the throws actually touched change, on the
// pattern laid out along the timeline. When the chain closes, the edit becomes a loop: its
// length is the stretch of beats the chain touched, rounded up to a multiple of the current
// loop period (so the period control acts as a minimum). In a "3", touching beats 1-3 to make
// 5, 3, 1 gives the loop 531. All jugglers share the loop length.
//
// Closing on the hole's copy in another repeat (the same juggler, a whole number of periods
// away) is allowed when asked for explicitly (the UI uses Shift-click), if some loop length
// fits: it must be at least the stretch touched and divide the distance to the hole. The
// shortest such length is used. That makes one throw a whole loop higher or lower, adding or
// removing props (3 can become 9 this way; unroll to period 3 first to get 933). Without the
// request, a drop on a copy just picks up the throw that lands (or starts) there, like any
// other drop. (In a 1-beat loop such as "3", every beat is a copy of the hole, so this has to
// be opt-in.)
//
// Every drop exchanges where two throws land (or start), and the loop is never shorter than the
// stretch touched, so a closed chain always yields a valid pattern. Empty beats (0s) take part
// as throws that land on their own beat, in their own hand (a 0 can't be a pass).
//
// Beats are absolute beats on the ladder (0 = the first beat drawn) and may be negative.
// Nothing here draws or reads input; ladder_view does that and calls these.
#pragma once

#include "siteswap.h"

#include <map>
#include <vector>

enum class HeldEnd { Arrival, Departure };

constexpr int kNoThrow = -1;  // the value of a departure whose throw is currently held / missing

// Longest loop an edit may produce (and the period control may write out), in beats.
constexpr int kMaxLoopBeats = 64;

// A juggler's hand on a beat: where a throw leaves from or lands. (Which hand follows from the
// beat: every juggler throws right on even beats.)
struct Slot {
    int juggler = 0;
    int beat = 0;
    bool operator==(const Slot& o) const { return juggler == o.juggler && beat == o.beat; }
    bool operator!=(const Slot& o) const { return !(*this == o); }
    bool operator<(const Slot& o) const { return beat != o.beat ? beat < o.beat : juggler < o.juggler; }
};

// The arrival slot of a throw made from `from`.
inline Slot landingSlot(Slot from, const LoopThrow& t) { return Slot{t.dest, from.beat + t.value}; }

struct EditChain {
    bool active = false;
    HeldEnd end = HeldEnd::Arrival;
    int period = 0;                       // the loop's period when the chain started
    JugglingLoop base;                    // the loop when the chain started
    std::map<Slot, LoopThrow> overrides;  // departure slot -> new throw (value kNoThrow while held)

    // The held throw. For a held ARRIVAL, `fixed` is the slot it's thrown from (an override
    // set to kNoThrow until it's placed). For a held DEPARTURE, `fixed` is the slot it lands
    // in, and the hole is the departure slot it left empty.
    Slot fixed;
    LoopThrow held;  // the held throw as it was when picked up (value 0 = an empty beat)

    Slot hole;       // the empty spot: an arrival slot or a departure slot
};

int positiveMod(int a, int m);

// The throw made from a slot, with the chain's edits applied (value kNoThrow if held).
LoopThrow editThrowAt(const EditChain& chain, Slot departure);

// Starts a chain by picking up one end of the throw made from `throwSlot`.
EditChain beginEditChain(const JugglingLoop& loop, Slot throwSlot, HeldEnd end);

// The throw the held end would become if dropped on `target` (an arrival slot for a held
// arrival, a departure slot for a held departure), and the slot it would then be thrown from.
// Returns false if that isn't a legal drop: values must be 0-35, excluding 25 and 33 (their
// letters, p and x, mean passing and sync in siteswap notation, so they can't be written), and
// a 0 can't go to another juggler.
bool editDropThrow(const EditChain& chain, Slot target, LoopThrow* result, Slot* from = nullptr);

// If dropping on target would close the chain, returns the resulting loop length; otherwise 0
// (the drop, if legal, displaces whatever is there instead). closeOnCopy allows closing on the
// hole's copy in another repeat. Closes that would make a loop longer than kMaxLoopBeats are
// refused (0); editCloseLoopLengthUnlimited says how long it would have been.
int editCloseLoopLength(const EditChain& chain, Slot target, bool closeOnCopy);
int editCloseLoopLengthUnlimited(const EditChain& chain, Slot target, bool closeOnCopy);

// Prop-count change closing on target would make (0 on the hole itself), assuming closeOnCopy.
// Only meaningful when editCloseLoopLength(chain, target, true) > 0.
int editDropBallDelta(const EditChain& chain, Slot target);

// Loop length the edit would get if it closed with what's been touched so far.
int editProvisionalLoopLength(const EditChain& chain);

enum class DropResult { Invalid, Continued, Closed };

// Drops the held end on target. On Closed, *resultLoop receives the new loop (every juggler)
// and the chain is no longer active.
DropResult editDrop(EditChain& chain, Slot target, bool closeOnCopy, JugglingLoop* resultLoop);
