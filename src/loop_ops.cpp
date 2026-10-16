// loop_ops.cpp - see loop_ops.h.
#include "loop_ops.h"

#include <algorithm>
#include <cstdlib>
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

namespace {

// How a throw's target is written after its value ("", "p", "p+1", "p2").
std::string targetText(const ThrowForm& f, int jugglers) {
    switch (f.kind) {
    case ThrowForm::Kind::Plain:
        return "";
    case ThrowForm::Kind::BareP:
        return "p";
    case ThrowForm::Kind::Relative:
        return std::string("p") + (f.value < 0 ? "-" : "+") + std::to_string(f.value < 0 ? -f.value : f.value);
    case ThrowForm::Kind::Absolute:
        return jugglers > 2 ? "p" + std::to_string(f.value + 1) : "p";
    }
    return "";
}

// The form a throw is written in when there's no record of how it was typed: as Juggling Lab
// writes it.
ThrowForm plainForm(const LoopThrow& t, int juggler, int jugglers) {
    ThrowForm f;
    if (t.value == kOpenThrow || t.dest == juggler) return f;
    if (jugglers == 2) {
        f.kind = ThrowForm::Kind::BareP;
    } else {
        f.kind = ThrowForm::Kind::Absolute;
        f.value = t.dest;
    }
    return f;
}

// Whether juggler j's throws in `loop` are what their link in `form` makes of the throws of the
// juggler they copy.
bool linkHolds(const JugglingLoop& loop, const PatternForm& form, int j) {
    const PartLink& link = form.links[static_cast<size_t>(j)];
    for (int b = 0; b < loop.period; ++b) {
        const LoopThrow& source = loop.at(link.to, b + link.start);
        const ThrowForm f = form.formFor(link.to, b + link.start, source);
        if (carryThrow(f, source, link.to, j, loop.jugglers) != loop.at(j, b)) return false;
    }
    return true;
}

}  // namespace

bool loopToText(const JugglingLoop& loop, std::string* text, const PatternForm* form) {
    std::string out;
    if (loop.empty()) return false;
    for (const LoopThrow& t : loop.throws)
        if (t.value != kOpenThrow && !writableValue(t.value)) return false;
    // The record of how it was typed applies if it's for these jugglers, at this period or one
    // it's a multiple of (written out longer, its throws still line up).
    const bool typed = form && form->jugglers == loop.jugglers && form->period > 0 && loop.period % form->period == 0;
    if (loop.jugglers <= 1) {
        for (int b = 0; b < loop.period; ++b) out += valueChar(loop.at(0, b).value);
        if (loop.handsSwapped(0)) out += ",LRswap";
    } else {
        // Juggling Lab's passing notation: <3p 3|3p 3>.
        out = "<";
        for (int j = 0; j < loop.jugglers; ++j) {
            if (j > 0) out += '|';
            if (typed && form->linked(j) && linkHolds(loop, *form, j)) {
                const PartLink& link = form->links[static_cast<size_t>(j)];
                out += "@" + std::to_string(link.to + 1);
                const int start = positiveMod(link.start, loop.period);
                if (start != 0) out += "[" + std::to_string(start) + "]";
                if (loop.handsSwapped(j) != loop.handsSwapped(link.to)) out += ",LRswap";
                continue;
            }
            for (int b = 0; b < loop.period; ++b) {
                const LoopThrow& t = loop.at(j, b);
                if (b > 0) out += ' ';
                out += valueChar(t.value);
                ThrowForm f = typed ? form->formFor(j, b, t) : plainForm(t, j, loop.jugglers);
                if (t.value != kOpenThrow && carryThrow(f, t, j, j, loop.jugglers) != t) f = plainForm(t, j, loop.jugglers);
                if (t.value != kOpenThrow) out += targetText(f, loop.jugglers);
            }
            if (loop.handsSwapped(j)) out += ",LRswap";
        }
        out += '>';
    }
    *text = out;
    return true;
}

JugglingLoop applyLinks(const PatternForm& form, const JugglingLoop& loop) {
    JugglingLoop out = loop;
    if (!form.hasLinks() || form.jugglers != loop.jugglers || loop.empty()) return out;
    out.swapHands.resize(static_cast<size_t>(loop.jugglers), 0);
    std::vector<char> done(static_cast<size_t>(loop.jugglers), 0);
    for (int j = 0; j < loop.jugglers; ++j) done[static_cast<size_t>(j)] = form.linked(j) ? 0 : 1;
    for (int pass = 0; pass < loop.jugglers; ++pass) {
        for (int j = 0; j < loop.jugglers; ++j) {
            const PartLink& link = form.links[static_cast<size_t>(j)];
            if (done[static_cast<size_t>(j)] || !done[static_cast<size_t>(link.to)]) continue;
            for (int b = 0; b < loop.period; ++b) {
                const LoopThrow source = out.at(link.to, b + link.start);
                const ThrowForm f = form.formFor(link.to, b + link.start, source);
                out.throws[static_cast<size_t>(j * loop.period + b)] = carryThrow(f, source, link.to, j, loop.jugglers);
            }
            out.swapHands[static_cast<size_t>(j)] = static_cast<char>(out.handsSwapped(link.to) != link.lrSwap);
            done[static_cast<size_t>(j)] = 1;
        }
    }
    return out;
}

bool projectEdit(const PatternForm& form, const JugglingLoop& before, const JugglingLoop& after,
                 JugglingLoop* result, std::string* why) {
    if (!form.hasLinks() || form.jugglers != after.jugglers || before.empty()) {
        *result = after;
        return true;
    }
    if (after.period % before.period != 0) {
        if (why)
            *why = "Adding or deleting beats isn't possible with linked jugglers yet. Unlink them first "
                   "(right-click a juggler's number).";
        return false;
    }
    const JugglingLoop base = before.period == after.period ? before : loopWithPeriod(before, after.period);
    const int period = after.period, jugglers = after.jugglers;
    // Each changed throw goes to the part written out that it's (a copy of) a throw of.
    JugglingLoop roots = base;
    std::vector<char> rootChanged(roots.throws.size(), 0);
    std::vector<int> changed;
    for (int j = 0; j < jugglers; ++j) {
        for (int b = 0; b < period; ++b) {
            const LoopThrow& t = after.at(j, b);
            if (t == base.at(j, b)) continue;
            changed.push_back(j * period + b);
            int root = j, start = 0;
            if (!form.root(j, &root, &start)) {
                if (why) *why = "The links between these jugglers go round in a loop.";
                return false;
            }
            const int rootBeat = positiveMod(b + start, period);
            const LoopThrow rootThrow = carryThrow(form.formFor(j, b, t), t, j, root, jugglers);
            const size_t slot = static_cast<size_t>(root * period + rootBeat);
            if (rootChanged[slot] && roots.throws[slot] != rootThrow) {
                if (why) *why = "Those changes to linked jugglers don't agree with each other.";
                return false;
            }
            roots.throws[slot] = rootThrow;
            rootChanged[slot] = 1;
        }
    }
    const JugglingLoop out = applyLinks(form, roots);
    for (const int slot : changed) {
        if (out.throws[static_cast<size_t>(slot)] != after.throws[static_cast<size_t>(slot)]) {
            if (why) *why = "That change can't be made to every linked juggler at once.";
            return false;
        }
    }
    const std::string problem = loopProblem(out);
    if (!problem.empty()) {
        if (why) *why = "Every linked juggler would follow, and then: " + problem;
        return false;
    }
    *result = out;
    return true;
}

bool linkJuggler(const PatternForm& form, const JugglingLoop& loop, int j, int to, int start, PatternForm* newForm,
                 JugglingLoop* newLoop, std::string* why, bool allowProblems) {
    if (j < 0 || to < 0 || j >= loop.jugglers || to >= loop.jugglers || j == to) return false;
    PatternForm f = form;
    if (f.jugglers != loop.jugglers || static_cast<int>(f.links.size()) != loop.jugglers) {
        // No record of how it was typed (or for other jugglers): start one with nothing linked.
        f = PatternForm();
        f.jugglers = loop.jugglers;
        f.links.assign(static_cast<size_t>(loop.jugglers), PartLink());
    }
    f.links[static_cast<size_t>(j)].to = to;
    f.links[static_cast<size_t>(j)].start = start;
    f.links[static_cast<size_t>(j)].lrSwap = false;
    int root = 0, total = 0;
    if (!f.root(j, &root, &total)) {
        if (why) *why = "J" + std::to_string(to + 1) + " already copies J" + std::to_string(j + 1) + " (through links).";
        return false;
    }
    const JugglingLoop out = applyLinks(f, loop);
    const std::string problem = loopProblem(out);
    if (!problem.empty()) {
        if (why) *why = problem;
        if (!allowProblems) return false;
    }
    *newForm = f;
    *newLoop = out;
    return true;
}

PatternForm unlinkJuggler(const PatternForm& form, int j) {
    PatternForm f = form;
    if (j >= 0 && j < static_cast<int>(f.links.size())) f.links[static_cast<size_t>(j)] = PartLink();
    return f;
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
    out.swapHands = loop.swapHands;
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
    out.swapHands = loop.swapHands;
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
    out.swapHands = loop.swapHands;
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

int propCyclePeriod(const JugglingLoop& loop) {
    if (loop.empty() || openThrowCount(loop) > 0) return loop.period;
    const LoopOrbits orbits = computeLoopOrbits(loop);
    long long cycle = 1;
    for (const int props : orbits.propsOfOrbit) {
        if (props <= 0) continue;
        cycle = cycle / std::gcd(cycle, static_cast<long long>(props)) * props;
        if (cycle * loop.period > kMaxLoopBeats) return loop.period;
    }
    return static_cast<int>(cycle) * loop.period;
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

// ---- The throwing loop

ThrowLoopPlan throwLoopPlan(const std::vector<int>& next, int jugglers) {
    ThrowLoopPlan plan;
    const int n = std::max(jugglers, 1);
    auto identity = [&]() {
        plan.phases = 1;
        plan.part.assign(1, std::vector<int>(static_cast<size_t>(n)));
        for (int j = 0; j < n; ++j) plan.part[0][static_cast<size_t>(j)] = j;
    };
    identity();
    std::vector<int> takes(static_cast<size_t>(n));
    std::vector<std::vector<int>> takers(static_cast<size_t>(n));
    std::string problem;
    auto add = [&](const std::string& s) { problem += (problem.empty() ? "" : "; ") + s; };
    for (int j = 0; j < n; ++j) {
        const int r = j < static_cast<int>(next.size()) && next[static_cast<size_t>(j)] >= 0 ? next[static_cast<size_t>(j)] : j;
        takes[static_cast<size_t>(j)] = r;
        if (r >= n) {
            add("T" + std::to_string(j + 1) + " goes on to T" + std::to_string(r + 1) + ", and there's no T" +
                std::to_string(r + 1));
            plan.bad.push_back(j);
            continue;
        }
        takers[static_cast<size_t>(r)].push_back(j);
    }
    for (int r = 0; r < n; ++r) {
        const std::vector<int>& t = takers[static_cast<size_t>(r)];
        if (t.size() == 1) continue;
        if (t.empty()) {
            add("nothing goes on to T" + std::to_string(r + 1));
            continue;
        }
        std::string who;
        for (size_t i = 0; i < t.size(); ++i) {
            who += (i == 0 ? "T" : (i + 1 == t.size() ? " and T" : ", T")) + std::to_string(t[i] + 1);
            plan.bad.push_back(t[i]);
        }
        add(who + (t.size() == 2 ? " both" : " all") + " go on to T" + std::to_string(r + 1));
    }
    if (!problem.empty()) {
        plan.valid = false;
        plan.problem = problem;
        return plan;
    }
    for (int p = 0; p < ThrowLoopPlan::kMaxPhases; ++p) {
        std::vector<int> parts(static_cast<size_t>(n));
        bool home = true;
        for (int j = 0; j < n; ++j) {
            parts[static_cast<size_t>(j)] = plan.part[static_cast<size_t>(p)][static_cast<size_t>(takes[static_cast<size_t>(j)])];
            home = home && parts[static_cast<size_t>(j)] == j;
        }
        if (home) {
            plan.phases = p + 1;
            return plan;
        }
        plan.part.push_back(parts);
    }
    identity();  // (a permutation always comes home well before this; kept for safety)
    plan.valid = false;
    plan.problem = "it doesn't come back to the start";
    return plan;
}

bool WalkPaths::trivial() const {
    if (cycle <= 0 || path.empty()) return true;
    for (const std::vector<int>& phase : path)
        for (size_t j = 0; j < phase.size(); ++j)
            if (phase[j] != static_cast<int>(j)) return false;
    return true;
}

int WalkPaths::walker(int d, int beat) const {
    if (trivial()) return d;
    const int whole = cycle * static_cast<int>(path.size());
    const std::vector<int>& phase = path[static_cast<size_t>(positiveMod(beat, whole) / cycle)];
    for (size_t x = 0; x < phase.size(); ++x)
        if (phase[x] == d) return static_cast<int>(x);
    return d;
}

int WalkPaths::pathOf(int j, int beat) const {
    if (trivial()) return j;
    const int whole = cycle * static_cast<int>(path.size());
    const std::vector<int>& phase = path[static_cast<size_t>(positiveMod(beat, whole) / cycle)];
    return j >= 0 && j < static_cast<int>(phase.size()) ? phase[static_cast<size_t>(j)] : j;
}

int PlayedLoop::partAt(int juggler, int beat) const {
    if (period <= 0 || part.empty()) return juggler;
    const int round = (beat - positiveMod(beat, period)) / period;
    const std::vector<int>& phase = part[static_cast<size_t>(positiveMod(round, static_cast<int>(part.size())))];
    return juggler >= 0 && juggler < static_cast<int>(phase.size()) ? phase[static_cast<size_t>(juggler)] : juggler;
}

namespace {
int gcdOf(int a, int b) {
    while (b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}
}  // namespace

PlayedLoop playedLoop(const JugglingLoop& written, const ThrowLoopPlan& plan, const WalkPaths& paths) {
    PlayedLoop out;
    out.loop = written;
    out.period = written.period;
    out.part.assign(1, std::vector<int>());
    for (int j = 0; j < written.jugglers; ++j) out.part[0].push_back(j);
    if (written.empty() || written.jugglers < 2 || openThrowCount(written) > 0) return out;
    const bool walking = !paths.trivial();
    if (plan.phases <= 1 && !walking) return out;
    const int n = written.jugglers, P = written.period;
    long long length = static_cast<long long>(P) * std::max(1, plan.phases);
    if (walking) {
        const long long whole = static_cast<long long>(paths.cycle) * static_cast<long long>(paths.path.size());
        length = length / gcdOf(static_cast<int>(length), static_cast<int>(whole)) * whole;
    }
    if (length > kMaxPlayedBeats) {
        out.ok = false;
        out.problem = "the throwing and walking loops only come round together after " + std::to_string(length) +
                      " beats (more than " + std::to_string(kMaxPlayedBeats) + ")";
        return out;
    }
    const int L = static_cast<int>(length);
    if (plan.phases > 1) out.part = plan.part;
    JugglingLoop played;
    played.jugglers = n;
    played.period = L;
    played.swapHands = written.swapHands;
    played.throws.assign(static_cast<size_t>(n * L), LoopThrow());
    for (int j = 0; j < n; ++j) {
        for (int g = 0; g < L; ++g) {
            const int r = out.partAt(j, g);
            const LoopThrow& t = written.at(r, g);
            LoopThrow made = t;
            if (t.value <= 0 || t.dest == r) {
                made.dest = j;  // a self (or an empty hand) stays with the thrower
            } else {
                made.dest = paths.walker(t.dest, g + t.value);
            }
            played.throws[static_cast<size_t>(j * L + g)] = made;
        }
    }
    out.loop = played;
    out.expanded = played != written;
    // Does it work? Each hand with a throw to make gets exactly one prop, in time.
    std::vector<int> landing(static_cast<size_t>(n * L), 0);
    std::vector<int> firstFrom(static_cast<size_t>(n * L), -1);
    std::string twice, empty;
    for (int j = 0; j < n; ++j) {
        for (int g = 0; g < L; ++g) {
            const LoopThrow& t = played.throws[static_cast<size_t>(j * L + g)];
            if (t.value <= 0) continue;
            const size_t s = static_cast<size_t>(t.dest * L + positiveMod(g + t.value, L));
            if (++landing[s] == 1) {
                firstFrom[s] = j * L + g;
            } else if (twice.empty()) {
                const int at = positiveMod(g + t.value, L);
                twice = "J" + std::to_string(t.dest + 1) + " catches two props to throw on beat " + std::to_string(at + 1) +
                        " (J" + std::to_string(firstFrom[s] / L + 1) + "'s from beat " + std::to_string(firstFrom[s] % L + 1) +
                        " and J" + std::to_string(j + 1) + "'s from beat " + std::to_string(g + 1) + ")";
                out.clash.push_back({firstFrom[s] / L, firstFrom[s] % L});
                out.clash.push_back({j, g});
            }
        }
    }
    for (int j = 0; j < n && empty.empty(); ++j)
        for (int g = 0; g < L && empty.empty(); ++g)
            if (played.throws[static_cast<size_t>(j * L + g)].value > 0 && landing[static_cast<size_t>(j * L + g)] == 0) {
                empty = "J" + std::to_string(j + 1) + " has nothing to throw on beat " + std::to_string(g + 1);
                out.clash.push_back({j, g});
            }
    if (!twice.empty() || !empty.empty()) {
        out.ok = false;
        out.problem = twice + (!twice.empty() && !empty.empty() ? "; " : "") + empty;
    }
    return out;
}

bool writtenFromPlayed(const PlayedLoop& before, const JugglingLoop& written, const JugglingLoop& edited,
                       const WalkPaths& paths, JugglingLoop* out, std::string* why) {
    const JugglingLoop& was = before.loop;
    if (edited.jugglers != was.jugglers || edited.period != was.period) {
        *why = "not while the next column loops the throws (the ladder shows the loop played out; change the period in "
               "the grid).";
        return false;
    }
    const int n = was.jugglers, P = written.period, L = was.period;
    JugglingLoop result = written;
    for (int j = 0; j < n; ++j) {
        for (int g = 0; g < L; ++g) {
            const LoopThrow& t = edited.throws[static_cast<size_t>(j * L + g)];
            if (t == was.throws[static_cast<size_t>(j * L + g)]) continue;
            if (g >= P) {
                *why = "beat " + std::to_string(g + 1) + " is in the throwing loop's copies: only beats 1-" + std::to_string(P) +
                       " are written out (the next column, after beat " + std::to_string(P) + ", says what comes after).";
                return false;
            }
            LoopThrow w = t;
            if (t.value > 0 && t.dest != j) {
                w.dest = paths.pathOf(t.dest, g + t.value);
                if (w.dest == j) {
                    *why = "J" + std::to_string(t.dest + 1) + " walks J" + std::to_string(j + 1) + "'s own path when that lands.";
                    return false;
                }
            } else {
                w.dest = j;
            }
            result.throws[static_cast<size_t>(j * P + g)] = w;
        }
    }
    // (Anything in the copies a written throw changes comes from the written throws.)
    *out = result;
    return true;
}

std::string throwNextText(const std::vector<int>& next) {
    size_t last = 0;
    for (size_t j = 0; j < next.size(); ++j)
        if (next[j] >= 0 && next[j] != static_cast<int>(j)) last = j + 1;
    std::string out;
    for (size_t j = 0; j < last; ++j) {
        if (j > 0) out += ',';
        if (next[j] >= 0 && next[j] != static_cast<int>(j)) out += std::to_string(next[j] + 1);
    }
    return out;
}

bool readThrowNext(const std::string& text, std::vector<int>* next) {
    std::vector<int> out;
    std::string item;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ',') {
            if (item.empty()) {
                out.push_back(-1);
            } else {
                for (const char c : item)
                    if (c < '0' || c > '9') return false;
                const int k = std::atoi(item.c_str());
                if (k < 1 || k > kMaxJugglers) return false;
                out.push_back(k - 1 == static_cast<int>(out.size()) ? -1 : k - 1);
            }
            item.clear();
        } else {
            item += text[i];
        }
    }
    *next = out;
    return true;
}
