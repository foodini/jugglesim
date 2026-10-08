// loop_ops.cpp - see loop_ops.h.
#include "loop_ops.h"

#include <algorithm>
#include <numeric>

namespace {

bool writableValue(int value) { return value >= 0 && value <= 35 && value != 25 && value != 33; }

char valueChar(int value) {
    if (value == kOpenThrow) return '?';
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

int slotIndex(const JugglingLoop& loop, int juggler, int beat) {
    return juggler * loop.period + positiveMod(beat, loop.period);
}

// Union-find for orbits.
int findRoot(std::vector<int>& parent, int x) {
    while (parent[static_cast<size_t>(x)] != x) {
        parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
        x = parent[static_cast<size_t>(x)];
    }
    return x;
}

}  // namespace

int openThrowCount(const JugglingLoop& loop) {
    int open = 0;
    for (const LoopThrow& t : loop.throws)
        if (t.value == kOpenThrow) ++open;
    return open;
}

bool loopToText(const JugglingLoop& loop, std::string* text, const PassStyle* style) {
    std::string out;
    if (loop.empty()) return false;
    for (const LoopThrow& t : loop.throws)
        if (t.value != kOpenThrow && !writableValue(t.value)) return false;
    if (loop.jugglers <= 1) {
        for (int b = 0; b < loop.period; ++b) out += valueChar(loop.at(0, b).value);
    } else {
        // Juggling Lab's passing notation: <3p 3|3p 3>.
        out = "<";
        for (int j = 0; j < loop.jugglers; ++j) {
            if (j > 0) out += '|';
            for (int b = 0; b < loop.period; ++b) {
                const LoopThrow& t = loop.at(j, b);
                if (b > 0) out += ' ';
                out += valueChar(t.value);
                const bool relative = style && style->relative;
                if (t.value != kOpenThrow && t.dest != j) {
                    out += 'p';
                    if (relative)
                        out += '+' + std::to_string(((t.dest - j) % loop.jugglers + loop.jugglers) % loop.jugglers);
                    else if (loop.jugglers > 2)
                        out += std::to_string(t.dest + 1);
                } else if (t.value != kOpenThrow && style && style->marked(j, b, loop.jugglers, loop.period)) {
                    // A self written as a pass to yourself (to line up columns), kept that way.
                    out += relative ? "p+0" : "p" + std::to_string(j + 1);
                }
            }
        }
        out += '>';
    }
    *text = out;
    return true;
}

int loopShortestPeriod(const JugglingLoop& loop) {
    const int period = loop.period;
    for (int candidate = 1; candidate < period; ++candidate) {
        if (period % candidate != 0) continue;
        bool repeats = true;
        for (int j = 0; j < loop.jugglers && repeats; ++j)
            for (int i = candidate; i < period && repeats; ++i) repeats = loop.at(j, i) == loop.at(j, i - candidate);
        if (repeats) return candidate;
    }
    return period;
}

JugglingLoop loopWithPeriod(const JugglingLoop& loop, int period) {
    const int shortest = loopShortestPeriod(loop);
    if (shortest == 0 || period <= 0 || period % shortest != 0) return loop;
    JugglingLoop out;
    out.jugglers = loop.jugglers;
    out.period = period;
    for (int j = 0; j < loop.jugglers; ++j)
        for (int i = 0; i < period; ++i) out.throws.push_back(loop.at(j, i % shortest));
    return out;
}

std::vector<int> loopIncoming(const JugglingLoop& loop) {
    std::vector<int> incoming(loop.throws.size(), -1);
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = 0; b < loop.period; ++b) {
            const LoopThrow& t = loop.at(j, b);
            if (t.value == kOpenThrow) continue;
            incoming[static_cast<size_t>(slotIndex(loop, t.dest, b + t.value))] = slotIndex(loop, j, b);
        }
    }
    return incoming;
}

std::vector<int> loopPathSlots(const JugglingLoop& loop, int slot) {
    std::vector<int> path;
    if (loop.empty() || slot < 0 || slot >= static_cast<int>(loop.throws.size())) return path;
    std::vector<bool> seen(loop.throws.size(), false);
    path.push_back(slot);
    seen[static_cast<size_t>(slot)] = true;
    // Forward: where the prop goes next, until it comes back or the throw isn't decided.
    int cur = slot;
    for (;;) {
        const LoopThrow& t = loop.throws[static_cast<size_t>(cur)];
        if (t.value == kOpenThrow) break;
        const int next = slotIndex(loop, t.dest, cur % loop.period + t.value);
        if (seen[static_cast<size_t>(next)]) break;
        seen[static_cast<size_t>(next)] = true;
        path.push_back(next);
        cur = next;
    }
    // Back: where it came from.
    const std::vector<int> incoming = loopIncoming(loop);
    cur = slot;
    for (;;) {
        const int prev = incoming[static_cast<size_t>(cur)];
        if (prev < 0 || seen[static_cast<size_t>(prev)]) break;
        seen[static_cast<size_t>(prev)] = true;
        path.push_back(prev);
        cur = prev;
    }
    return path;
}

int LoopOrbits::idAt(int juggler, int beat) const {
    if (length <= 0) return -1;
    const size_t i = static_cast<size_t>(juggler * length + positiveMod(beat, length));
    return i < idOfSpot.size() ? idOfSpot[i] : -1;
}

LoopOrbits computeLoopOrbits(const JugglingLoop& loop) {
    LoopOrbits o;
    if (loop.empty()) return o;
    o.length = loop.period;
    const int spots = loop.jugglers * o.length;
    std::vector<int> parent(static_cast<size_t>(spots));
    std::iota(parent.begin(), parent.end(), 0);
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = 0; b < o.length; ++b) {
            const LoopThrow& t = loop.at(j, b);
            if (t.value <= 0) continue;  // empty hand, or not decided
            const int from = j * o.length + b;
            const int to = t.dest * o.length + (b + t.value) % o.length;
            parent[static_cast<size_t>(findRoot(parent, from))] = findRoot(parent, to);
        }
    }
    // Number the orbits in order of first appearance (beat by beat, juggler by juggler), so
    // colors follow the ladder from the top.
    o.idOfSpot.assign(static_cast<size_t>(spots), -1);
    std::vector<int> idOfRoot(static_cast<size_t>(spots), -1);
    std::vector<int> valueSum;
    const bool complete = openThrowCount(loop) == 0;
    for (int b = 0; b < o.length; ++b) {
        for (int j = 0; j < loop.jugglers; ++j) {
            const LoopThrow& t = loop.at(j, b);
            if (t.value <= 0) continue;
            const int spot = j * o.length + b;
            const int root = findRoot(parent, spot);
            if (idOfRoot[static_cast<size_t>(root)] < 0) {
                idOfRoot[static_cast<size_t>(root)] = o.count++;
                valueSum.push_back(0);
            }
            const int id = idOfRoot[static_cast<size_t>(root)];
            o.idOfSpot[static_cast<size_t>(spot)] = id;
            valueSum[static_cast<size_t>(id)] += t.value;
        }
    }
    for (const int sum : valueSum) o.propsOfOrbit.push_back(complete ? sum / o.length : 0);
    return o;
}

std::vector<int> orbitOfEachBall(const JugglingLoop& loop, const BallOrbits& balls, const LoopOrbits& orbits) {
    std::vector<int> orbitOf(static_cast<size_t>(std::max(0, balls.totalBalls)), 0);
    // Every prop is thrown from some spot within (props + 1) periods.
    for (int b = 0; b < loop.period * (balls.totalBalls + 1); ++b) {
        for (int j = 0; j < loop.jugglers; ++j) {
            const int ball = orbitBallAt(balls, j, b);
            if (ball >= 0 && ball < balls.totalBalls) orbitOf[static_cast<size_t>(ball)] = std::max(0, orbits.idAt(j, b));
        }
    }
    return orbitOf;
}

bool insertBeats(const JugglingLoop& loop, int beforeBeat, int count, JugglingLoop* result, std::string* why) {
    if (loop.empty() || count <= 0) return false;
    const int p = loop.period;
    const int k = positiveMod(beforeBeat, p);  // the new beats go just before this position
    JugglingLoop out;
    out.jugglers = loop.jugglers;
    out.period = p + count;
    out.throws.resize(static_cast<size_t>(out.jugglers * out.period));
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int i = 0; i < count; ++i) out.throws[static_cast<size_t>(j * out.period + k + i)] = {0, j};
        for (int b = 0; b < p; ++b) {
            LoopThrow t = loop.at(j, b);
            if (t.value > 0) {
                // Copies of the insertion point at k + m*p; a throw spans those in (b, b + value].
                auto floorDiv = [](int a, int m) { return a >= 0 ? a / m : -((-a + m - 1) / m); };
                const int spans = floorDiv(b + t.value - k, p) - floorDiv(b - k, p);
                t.value += count * spans;
                if (!writableValue(t.value)) {
                    if (why)
                        *why = "J" + std::to_string(j + 1) + "'s throw on beat " + std::to_string(b + 1) +
                               " would become a " + std::to_string(t.value) +
                               (t.value > 35 ? ", higher than siteswap can write (35)."
                                             : ", which siteswap can't write (its letter means passing or sync).");
                    return false;
                }
            }
            const int nb = b >= k ? b + count : b;
            out.throws[static_cast<size_t>(j * out.period + nb)] = t;
        }
    }
    if (result) *result = out;
    return true;
}

bool deleteBeats(const JugglingLoop& loop, int firstBeat, int count, JugglingLoop* result, std::string* why,
                 int* propsRemoved) {
    if (propsRemoved) *propsRemoved = 0;
    if (loop.empty() || count <= 0) return false;
    const int p = loop.period;
    if (count >= p) {
        if (why) *why = "That would leave nothing: the loop is only " + std::to_string(p) + " beat" + (p == 1 ? "" : "s") + " long.";
        return false;
    }
    std::vector<bool> deleted(static_cast<size_t>(p), false);
    for (int i = 0; i < count; ++i) deleted[static_cast<size_t>(positiveMod(firstBeat + i, p))] = true;
    auto isDeleted = [&](int beat) { return deleted[static_cast<size_t>(positiveMod(beat, p))]; };
    std::vector<int> newPos(static_cast<size_t>(p), -1);
    int kept = 0;
    for (int b = 0; b < p; ++b)
        if (!deleted[static_cast<size_t>(b)]) newPos[static_cast<size_t>(b)] = kept++;

    JugglingLoop out;
    out.jugglers = loop.jugglers;
    out.period = kept;
    out.throws.resize(static_cast<size_t>(out.jugglers * kept));
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = 0; b < p; ++b) {
            if (deleted[static_cast<size_t>(b)]) continue;
            LoopThrow t = loop.at(j, b);
            if (t.value > 0) {
                // Follow the prop past deleted beats to where it's next caught on a kept one.
                int curJ = t.dest, curB = b + t.value;
                bool open = false;
                for (int guard = 0; isDeleted(curB) && guard < loop.jugglers * p + 2; ++guard) {
                    const LoopThrow& next = loop.at(curJ, curB);
                    if (next.value == kOpenThrow) {
                        open = true;
                        break;
                    }
                    curB += next.value;
                    curJ = next.dest;
                }
                if (open) {
                    t = {kOpenThrow, j};
                } else {
                    int skipped = 0;
                    for (int x = b + 1; x < curB; ++x)
                        if (isDeleted(x)) ++skipped;
                    t.value = curB - b - skipped;
                    t.dest = curJ;
                    if (!writableValue(t.value)) {
                        if (why)
                            *why = "J" + std::to_string(j + 1) + "'s throw on beat " + std::to_string(b + 1) +
                                   " would become a " + std::to_string(t.value) +
                                   (t.value > 35 ? ", higher than siteswap can write (35)."
                                                 : ", which siteswap can't write (its letter means passing or sync).");
                        return false;
                    }
                }
            }
            out.throws[static_cast<size_t>(j * kept + newPos[static_cast<size_t>(b)])] = t;
        }
    }
    if (propsRemoved && openThrowCount(loop) == 0 && openThrowCount(out) == 0)
        *propsRemoved = loopPropCount(loop) - loopPropCount(out);
    if (result) *result = out;
    return true;
}

JugglingLoop deleteThrow(const JugglingLoop& loop, int slot) {
    JugglingLoop out = loop;
    if (slot >= 0 && slot < static_cast<int>(out.throws.size()))
        out.throws[static_cast<size_t>(slot)] = {kOpenThrow, slot / loop.period};
    return out;
}

JugglingLoop deletePath(const JugglingLoop& loop, int slot) {
    JugglingLoop out = loop;
    for (const int s : loopPathSlots(loop, slot)) out.throws[static_cast<size_t>(s)] = {kOpenThrow, s / loop.period};
    return out;
}

bool drawnThrowFor(Slot from, Slot target, LoopThrow* t) {
    LoopThrow d;
    d.value = target.beat - from.beat;
    d.dest = target.juggler;
    if (!writableValue(d.value)) return false;
    if (d.value == 0 && d.dest != from.juggler) return false;  // a 0 is an empty hand
    if (t) *t = d;
    return true;
}

bool drawThrow(const JugglingLoop& loop, Slot from, Slot target, JugglingLoop* result, bool* displacedAny,
               Slot* displaced) {
    if (displacedAny) *displacedAny = false;
    if (loop.empty()) return false;
    const int fromSlot = slotIndex(loop, from.juggler, from.beat);
    if (loop.throws[static_cast<size_t>(fromSlot)].value != kOpenThrow) return false;
    LoopThrow t;
    if (!drawnThrowFor(from, target, &t)) return false;
    JugglingLoop out = loop;
    const int targetSlot = slotIndex(loop, target.juggler, target.beat);
    const int lander = loopIncoming(loop)[static_cast<size_t>(targetSlot)];
    if (lander >= 0) {
        const LoopThrow& old = loop.throws[static_cast<size_t>(lander)];
        if (displacedAny) *displacedAny = true;
        if (displaced) *displaced = Slot{lander / loop.period, target.beat - old.value};
        out.throws[static_cast<size_t>(lander)] = {kOpenThrow, lander / loop.period};
    }
    out.throws[static_cast<size_t>(fromSlot)] = t;
    if (result) *result = out;
    return true;
}

bool rethrowFrom(const JugglingLoop& loop, Slot from, Slot landing, JugglingLoop* result, bool* displacedAny,
                 Slot* displacedLanding) {
    if (displacedAny) *displacedAny = false;
    if (loop.empty()) return false;
    LoopThrow t;
    if (!drawnThrowFor(from, landing, &t)) return false;
    const int fromSlot = slotIndex(loop, from.juggler, from.beat);
    const LoopThrow old = loop.throws[static_cast<size_t>(fromSlot)];
    JugglingLoop out = loop;
    if (old.value > 0) {  // (an empty hand, 0, just gives way: there's no prop to re-home)
        if (displacedAny) *displacedAny = true;
        if (displacedLanding) *displacedLanding = Slot{old.dest, from.beat + old.value};
    }
    out.throws[static_cast<size_t>(fromSlot)] = t;
    if (result) *result = out;
    return true;
}
