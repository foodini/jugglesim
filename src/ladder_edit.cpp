// ladder_edit.cpp - see ladder_edit.h.
#include "ladder_edit.h"

#include <climits>
#include <cstdlib>

namespace {

// Departure beats the chain has touched, including the beat a pending drop would add.
// skipBeat (if not INT_MIN) is left out: a held departure's empty spot, when the chain closes on
// its copy in another repeat (the two become the same loop slot).
void touchedRange(const EditChain& chain, int extraBeat, bool useExtra, int skipBeat, int* lo,
                  int* hi) {
    bool any = false;
    for (const std::pair<const int, int>& o : chain.overrides) {
        if (o.first == skipBeat) continue;
        if (!any || o.first < *lo) *lo = o.first;
        if (!any || o.first > *hi) *hi = o.first;
        any = true;
    }
    if (useExtra) {
        if (!any || extraBeat < *lo) *lo = extraBeat;
        if (!any || extraBeat > *hi) *hi = extraBeat;
        any = true;
    }
    if (!any) *lo = *hi = chain.fixedBeat;
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

}  // namespace

int positiveMod(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

int editValueAt(const EditChain& chain, int beat) {
    std::map<int, int>::const_iterator it = chain.overrides.find(beat);
    if (it != chain.overrides.end()) return it->second;
    return chain.base[static_cast<size_t>(positiveMod(beat, chain.period))];
}

EditChain beginEditChain(const std::vector<int>& loop, int throwBeat, HeldEnd end) {
    EditChain c;
    c.period = static_cast<int>(loop.size());
    if (c.period == 0) return c;
    c.active = true;
    c.end = end;
    c.base = loop;
    c.heldValue = loop[static_cast<size_t>(positiveMod(throwBeat, c.period))];
    c.overrides[throwBeat] = kNoThrow;
    if (end == HeldEnd::Arrival) {
        c.fixedBeat = throwBeat;               // stays thrown from here
        c.holeBeat = throwBeat + c.heldValue;  // its landing spot is now empty
    } else {
        c.fixedBeat = throwBeat + c.heldValue; // still lands here
        c.holeBeat = throwBeat;                // nothing is thrown from here now
    }
    return c;
}

bool editDropValue(const EditChain& chain, int targetBeat, int* value) {
    if (!chain.active) return false;
    const int v = chain.end == HeldEnd::Arrival ? targetBeat - chain.fixedBeat
                                                : chain.fixedBeat - targetBeat;
    if (v < 0 || v > 35 || v == 25 || v == 33) return false;
    if (value) *value = v;
    return true;
}

int editCloseLoopLength(const EditChain& chain, int targetBeat, bool closeOnCopy) {
    const int length = editCloseLoopLengthUnlimited(chain, targetBeat, closeOnCopy);
    return length <= kMaxLoopBeats ? length : 0;
}

int editCloseLoopLengthUnlimited(const EditChain& chain, int targetBeat, bool closeOnCopy) {
    if (!chain.active) return 0;
    const int distance = targetBeat - chain.holeBeat;
    if (positiveMod(distance, chain.period) != 0) return 0;
    if (distance != 0) {
        if (!closeOnCopy) return 0;
        // The copy must still be untouched by this chain: the throw starting there (held
        // departure) or landing there (held arrival) is one the chain hasn't moved. Otherwise
        // closing would overwrite an edit made earlier in the chain.
        if (chain.end == HeldEnd::Departure) {
            if (chain.overrides.count(targetBeat) != 0) return 0;
        } else {
            for (int d = targetBeat; d >= targetBeat - 35; --d) {
                const int v = editValueAt(chain, d);
                if (v != kNoThrow && d + v == targetBeat) {
                    if (chain.overrides.count(d) != 0) return 0;
                    break;
                }
            }
        }
    }
    // The departure the closing drop sets: the held throw's own (arrival mode) or the target.
    const int departure = chain.end == HeldEnd::Arrival ? chain.fixedBeat : targetBeat;
    const int skip = (chain.end == HeldEnd::Departure && distance != 0) ? chain.holeBeat : INT_MIN;
    int lo = 0, hi = 0;
    touchedRange(chain, departure, true, skip, &lo, &hi);
    return loopLengthFor(chain.period, hi - lo + 1, distance);
}

int editDropBallDelta(const EditChain& chain, int targetBeat) {
    const int length = editCloseLoopLength(chain, targetBeat, true);
    if (length == 0) return 0;
    // A throw a whole loop higher carries one more ball around the loop.
    const int shift = targetBeat - chain.holeBeat;
    return chain.end == HeldEnd::Arrival ? shift / length : -shift / length;
}

int editProvisionalLoopLength(const EditChain& chain) {
    int lo = 0, hi = 0;
    touchedRange(chain, 0, false, INT_MIN, &lo, &hi);
    return loopLengthFor(chain.period, hi - lo + 1, 0);
}

DropResult editDrop(EditChain& chain, int targetBeat, bool closeOnCopy,
                    std::vector<int>* resultLoop) {
    int value = 0;
    if (!editDropValue(chain, targetBeat, &value)) return DropResult::Invalid;
    const int length = editCloseLoopLength(chain, targetBeat, closeOnCopy);

    if (length > 0) {
        // Close: place the held throw, then fold the touched stretch into a loop.
        const int departure = chain.end == HeldEnd::Arrival ? chain.fixedBeat : targetBeat;
        if (chain.end == HeldEnd::Departure && departure != chain.holeBeat)
            chain.overrides.erase(chain.holeBeat);  // same loop slot as the target now
        chain.overrides[departure] = value;
        int lo = 0, hi = 0;
        touchedRange(chain, 0, false, INT_MIN, &lo, &hi);
        std::vector<int> loop(static_cast<size_t>(length));
        for (int b = lo; b < lo + length; ++b)
            loop[static_cast<size_t>(positiveMod(b, length))] = editValueAt(chain, b);
        if (resultLoop) *resultLoop = loop;
        chain.active = false;
        return DropResult::Closed;
    }

    if (chain.end == HeldEnd::Arrival) {
        // Find the throw (or 0) that lands exactly on targetBeat. In a valid pattern exactly one
        // does; values are at most 35, so it was thrown within the last 35 beats.
        int displaced = 0;
        bool found = false;
        for (int d = targetBeat; d >= targetBeat - 35 && !found; --d) {
            const int v = editValueAt(chain, d);
            if (v != kNoThrow && d + v == targetBeat) {
                displaced = d;
                found = true;
            }
        }
        if (!found) return DropResult::Invalid;  // can't happen in a valid pattern
        const int displacedValue = editValueAt(chain, displaced);
        chain.overrides[chain.fixedBeat] = value;
        chain.overrides[displaced] = kNoThrow;
        chain.heldValue = displacedValue;
        chain.fixedBeat = displaced;
        return DropResult::Continued;
    }

    // Held departure: displace whatever is thrown on targetBeat.
    const int displacedValue = editValueAt(chain, targetBeat);
    if (displacedValue == kNoThrow) return DropResult::Invalid;  // can't happen
    chain.overrides[targetBeat] = value;
    chain.heldValue = displacedValue;
    chain.fixedBeat = targetBeat + displacedValue;  // where the displaced throw still lands
    return DropResult::Continued;
}
