// ladder_edit.cpp - see ladder_edit.h.
#include "ladder_edit.h"

#include <climits>
#include <cstdlib>

namespace {

// Beats of the departures the chain has touched, including a pending drop's. skip (if
// skipValid) is left out: a held departure's empty spot, when the chain closes on its copy in
// another repeat (the two become the same loop slot).
void touchedRange(const EditChain& chain, int extraBeat, bool useExtra, Slot skip, bool skipValid,
                  int* lo, int* hi) {
    bool any = false;
    for (const std::pair<const Slot, LoopThrow>& o : chain.overrides) {
        if (skipValid && o.first == skip) continue;
        if (!any || o.first.beat < *lo) *lo = o.first.beat;
        if (!any || o.first.beat > *hi) *hi = o.first.beat;
        any = true;
    }
    if (useExtra) {
        if (!any || extraBeat < *lo) *lo = extraBeat;
        if (!any || extraBeat > *hi) *hi = extraBeat;
        any = true;
    }
    if (!any) *lo = *hi = chain.fixed.beat;
}

// Smallest multiple of the period that is >= span and divides `distance` (any, if 0).
int loopLengthFor(int period, int span, int distance) {
    if (period <= 0) return 0;
    const int minMultiple = (span + period - 1) / period;
    if (distance == 0) return (minMultiple < 1 ? 1 : minMultiple) * period;
    const int k = std::abs(distance) / period;  // distance is a multiple of the period
    for (int m = minMultiple < 1 ? 1 : minMultiple; m <= k; ++m)
        if (k % m == 0) return m * period;
    return 0;
}

// The departure slot of the throw that lands in `arrival` (with the chain's edits), if any.
// Values are at most 35, so it was thrown within the last 35 beats.
bool findThrowLandingIn(const EditChain& chain, Slot arrival, Slot* departure) {
    for (int d = arrival.beat; d >= arrival.beat - 35; --d) {
        for (int j = 0; j < chain.base.jugglers; ++j) {
            const Slot from{j, d};
            const LoopThrow t = editThrowAt(chain, from);
            if (t.value != kNoThrow && landingSlot(from, t) == arrival) {
                *departure = from;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

int positiveMod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

LoopThrow editThrowAt(const EditChain& chain, Slot departure) {
    std::map<Slot, LoopThrow>::const_iterator it = chain.overrides.find(departure);
    if (it != chain.overrides.end()) return it->second;
    return chain.base.at(departure.juggler, positiveMod(departure.beat, chain.period));
}

EditChain beginEditChain(const JugglingLoop& loop, Slot throwSlot, HeldEnd end) {
    EditChain c;
    c.period = loop.period;
    if (loop.empty()) return c;
    c.active = true;
    c.end = end;
    c.base = loop;
    c.held = loop.at(throwSlot.juggler, positiveMod(throwSlot.beat, c.period));
    LoopThrow none;
    none.value = kNoThrow;
    none.dest = throwSlot.juggler;
    c.overrides[throwSlot] = none;
    if (end == HeldEnd::Arrival) {
        c.fixed = throwSlot;                        // stays thrown from here
        c.hole = landingSlot(throwSlot, c.held);    // its landing spot is now empty
    } else {
        c.fixed = landingSlot(throwSlot, c.held);   // still lands here
        c.hole = throwSlot;                         // nothing is thrown from here now
    }
    return c;
}

bool editDropThrow(const EditChain& chain, Slot target, LoopThrow* result, Slot* from) {
    if (!chain.active) return false;
    LoopThrow t;
    Slot departure;
    if (chain.end == HeldEnd::Arrival) {
        departure = chain.fixed;
        t.value = target.beat - chain.fixed.beat;
        t.dest = target.juggler;
    } else {
        departure = target;
        t.value = chain.fixed.beat - target.beat;
        t.dest = chain.fixed.juggler;
    }
    if (t.value < 0 || t.value > 35 || t.value == 25 || t.value == 33) return false;
    if (t.value == 0 && t.dest != departure.juggler) return false;  // a 0 is an empty hand
    if (result) *result = t;
    if (from) *from = departure;
    return true;
}

int editCloseLoopLength(const EditChain& chain, Slot target, bool closeOnCopy) {
    const int length = editCloseLoopLengthUnlimited(chain, target, closeOnCopy);
    return length <= kMaxLoopBeats ? length : 0;
}

int editCloseLoopLengthUnlimited(const EditChain& chain, Slot target, bool closeOnCopy) {
    if (!chain.active || target.juggler != chain.hole.juggler) return 0;
    const int distance = target.beat - chain.hole.beat;
    if (positiveMod(distance, chain.period) != 0) return 0;
    if (distance != 0) {
        if (!closeOnCopy) return 0;
        // The copy must still be untouched by this chain: the throw starting there (held
        // departure) or landing there (held arrival) is one the chain hasn't moved. Otherwise
        // closing would overwrite an edit made earlier in the chain.
        if (chain.end == HeldEnd::Departure) {
            if (chain.overrides.count(target) != 0) return 0;
        } else {
            Slot lander;
            if (findThrowLandingIn(chain, target, &lander) && chain.overrides.count(lander) != 0) return 0;
        }
    }
    // The departure the closing drop sets: the held throw's own (arrival mode) or the target.
    const int departureBeat = chain.end == HeldEnd::Arrival ? chain.fixed.beat : target.beat;
    const bool skip = chain.end == HeldEnd::Departure && distance != 0;
    int lo = 0, hi = 0;
    touchedRange(chain, departureBeat, true, chain.hole, skip, &lo, &hi);
    return loopLengthFor(chain.period, hi - lo + 1, distance);
}

int editDropBallDelta(const EditChain& chain, Slot target) {
    const int length = editCloseLoopLength(chain, target, true);
    if (length == 0) return 0;
    // A throw a whole loop higher carries one more prop around the loop.
    const int shift = target.beat - chain.hole.beat;
    return chain.end == HeldEnd::Arrival ? shift / length : -shift / length;
}

int editProvisionalLoopLength(const EditChain& chain) {
    int lo = 0, hi = 0;
    touchedRange(chain, 0, false, Slot(), false, &lo, &hi);
    return loopLengthFor(chain.period, hi - lo + 1, 0);
}

DropResult editDrop(EditChain& chain, Slot target, bool closeOnCopy, JugglingLoop* resultLoop) {
    LoopThrow placed;
    Slot departure;
    if (!editDropThrow(chain, target, &placed, &departure)) return DropResult::Invalid;
    const int length = editCloseLoopLength(chain, target, closeOnCopy);

    if (length > 0) {
        // Close: place the held throw, then fold the touched stretch into a loop.
        if (chain.end == HeldEnd::Departure && departure != chain.hole)
            chain.overrides.erase(chain.hole);  // same loop slot as the target now
        chain.overrides[departure] = placed;
        int lo = 0, hi = 0;
        touchedRange(chain, 0, false, Slot(), false, &lo, &hi);
        JugglingLoop loop;
        loop.jugglers = chain.base.jugglers;
        loop.period = length;
        loop.throws.resize(static_cast<size_t>(loop.jugglers * length));
        for (int j = 0; j < loop.jugglers; ++j)
            for (int b = lo; b < lo + length; ++b)
                loop.throws[static_cast<size_t>(j * length + positiveMod(b, length))] = editThrowAt(chain, Slot{j, b});
        if (resultLoop) *resultLoop = loop;
        chain.active = false;
        return DropResult::Closed;
    }

    if (chain.end == HeldEnd::Arrival) {
        // Displace the throw (or 0) that lands exactly in the target. In a valid pattern
        // exactly one does.
        Slot displaced;
        if (!findThrowLandingIn(chain, target, &displaced)) return DropResult::Invalid;  // can't happen
        const LoopThrow displacedThrow = editThrowAt(chain, displaced);
        chain.overrides[chain.fixed] = placed;
        LoopThrow none;
        none.value = kNoThrow;
        none.dest = displaced.juggler;
        chain.overrides[displaced] = none;
        chain.held = displacedThrow;
        chain.fixed = displaced;
        return DropResult::Continued;
    }

    // Held departure: displace whatever is thrown from the target.
    const LoopThrow displacedThrow = editThrowAt(chain, target);
    if (displacedThrow.value == kNoThrow) return DropResult::Invalid;  // can't happen
    chain.overrides[target] = placed;
    chain.held = displacedThrow;
    chain.fixed = landingSlot(target, displacedThrow);  // where the displaced throw still lands
    return DropResult::Continued;
}
