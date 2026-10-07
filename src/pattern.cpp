// pattern.cpp - see pattern.h.
#include "pattern.h"

#include <algorithm>

namespace {

// Hand that throws on a given beat in async juggling: beat 0 is the right hand.
Hand asyncHandForBeat(int beat) { return beat % 2 == 0 ? Hand::Right : Hand::Left; }

}  // namespace

Pattern patternFromLoop(const JugglingLoop& loop) {
    Pattern p;
    p.jugglers = std::max(1, loop.jugglers);
    const int period = loop.period;
    // Physical period: an odd-length loop needs two passes to come back to the same hand.
    const int storedBeats = (period % 2 == 1) ? period * 2 : period;
    for (int beat = 0; beat < storedBeats; ++beat) {
        for (int j = 0; j < loop.jugglers; ++j) {
            const LoopThrow& t = loop.at(j, beat);
            if (t.value == 0) continue;  // empty hand: no event
            ThrowEvent e;
            e.juggler = j;
            e.hand = asyncHandForBeat(beat);
            e.throwTick = beatsToTicks(beat);
            e.value = t.value;
            e.destJuggler = t.dest;
            e.destHand = asyncHandForBeat(beat + t.value);
            e.spins = defaultSpinCount(t.value);
            p.events.push_back(e);
        }
    }
    Section s;
    s.start = 0;
    s.length = beatsToTicks(storedBeats);
    s.loopBeats = period;
    s.repeats = kRepeatForever;
    p.sections.push_back(s);
    return p;
}

Pattern patternFromLoopValues(const std::vector<int>& values) { return patternFromLoop(soloLoop(values)); }

JugglingLoop patternLoop(const Pattern& pattern) {
    JugglingLoop loop;
    loop.jugglers = pattern.jugglers;
    loop.period = loopPeriodBeats(pattern);
    loop.throws.assign(static_cast<size_t>(loop.jugglers * loop.period), LoopThrow());
    for (int j = 0; j < loop.jugglers; ++j)
        for (int b = 0; b < loop.period; ++b) loop.throws[static_cast<size_t>(j * loop.period + b)].dest = j;
    for (const ThrowEvent& e : pattern.events) {
        const Tick beat = e.throwTick / kTicksPerBeat;
        if (beat >= 0 && beat < loop.period && e.throwTick % kTicksPerBeat == 0 && e.juggler < loop.jugglers)
            loop.throws[static_cast<size_t>(e.juggler * loop.period + beat)] = {e.value, e.destJuggler};
    }
    return loop;
}

int defaultSpinCount(int value) {
    if (value <= 2) return 0;
    if (value == 3) return 1;
    return value / 2;
}

Pattern patternFromSiteswap(const Siteswap& siteswap) { return patternFromLoop(siteswap.loop); }

int loopPeriodBeats(const Pattern& pattern) {
    if (pattern.sections.empty()) return 0;
    return pattern.sections[0].loopBeats;
}

std::vector<int> loopThrowValues(const Pattern& pattern) {
    const int period = loopPeriodBeats(pattern);
    std::vector<int> values(static_cast<size_t>(period), 0);
    for (const ThrowEvent& e : pattern.events) {
        const Tick beat = e.throwTick / kTicksPerBeat;
        if (beat >= 0 && beat < period && e.throwTick % kTicksPerBeat == 0 && e.juggler == 0)
            values[static_cast<size_t>(beat)] = e.value;
    }
    return values;
}

int ballCount(const Pattern& pattern) {
    if (pattern.sections.empty()) return 0;
    const Tick storedBeats = pattern.sections[0].length / kTicksPerBeat;
    if (storedBeats == 0) return 0;
    Tick sum = 0;
    for (const ThrowEvent& e : pattern.events) sum += e.value;
    return static_cast<int>(sum / storedBeats);
}

int shortestPeriodBeats(const Pattern& pattern) {
    const JugglingLoop loop = patternLoop(pattern);
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

Pattern withPeriod(const Pattern& pattern, int periodBeats) {
    const JugglingLoop loop = patternLoop(pattern);
    const int shortest = shortestPeriodBeats(pattern);
    if (shortest == 0 || periodBeats <= 0 || periodBeats % shortest != 0) return pattern;
    JugglingLoop longer;
    longer.jugglers = loop.jugglers;
    longer.period = periodBeats;
    for (int j = 0; j < loop.jugglers; ++j)
        for (int i = 0; i < periodBeats; ++i) longer.throws.push_back(loop.at(j, i % shortest));
    return patternFromLoop(longer);
}

bool patternToSiteswap(const Pattern& pattern, std::string* text) {
    const JugglingLoop loop = patternLoop(pattern);
    auto valueChar = [](int value, char* out) {
        if (value < 0 || value > 35 || value == 25 || value == 33) return false;
        *out = value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
        return true;
    };
    std::string out;
    if (loop.jugglers <= 1) {
        for (int b = 0; b < loop.period; ++b) {
            char c;
            if (!valueChar(loop.at(0, b).value, &c)) return false;
            out += c;
        }
    } else {
        // Juggling Lab's passing notation: <3p 3|3p 3>.
        out = "<";
        for (int j = 0; j < loop.jugglers; ++j) {
            if (j > 0) out += '|';
            for (int b = 0; b < loop.period; ++b) {
                const LoopThrow& t = loop.at(j, b);
                char c;
                if (!valueChar(t.value, &c)) return false;
                if (b > 0) out += ' ';
                out += c;
                if (t.dest != j) {
                    out += 'p';
                    if (loop.jugglers > 2) out += std::to_string(t.dest + 1);
                }
            }
        }
        out += '>';
    }
    *text = out;
    return true;
}

namespace {
int modPositive(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}
}  // namespace

BallOrbits computeBallOrbits(const JugglingLoop& loop) {
    BallOrbits o;
    o.period = loop.period;
    o.jugglers = loop.jugglers;
    o.loop = loop;
    const size_t n = static_cast<size_t>(loop.period * loop.jugglers);
    o.start.assign(n, -1);
    o.offset.assign(n, 0);
    o.base.assign(n, 0);
    o.balls.assign(n, 0);
    if (loop.period == 0) return o;
    for (int s0 = 0; s0 < static_cast<int>(n); ++s0) {
        const LoopThrow& first = loop.throws[static_cast<size_t>(s0)];
        if (o.start[static_cast<size_t>(s0)] >= 0 || first.value <= 0) continue;
        std::vector<int> members;
        int cur = s0;
        int travelled = 0;
        while (o.start[static_cast<size_t>(cur)] < 0) {
            o.start[static_cast<size_t>(cur)] = s0;
            o.offset[static_cast<size_t>(cur)] = travelled;
            members.push_back(cur);
            const LoopThrow& t = loop.throws[static_cast<size_t>(cur)];
            travelled += t.value;
            cur = t.dest * loop.period + modPositive(cur % loop.period + t.value, loop.period);
        }
        const int balls = travelled / o.period;
        for (int m : members) {
            o.base[static_cast<size_t>(m)] = o.totalBalls;
            o.balls[static_cast<size_t>(m)] = balls;
        }
        o.totalBalls += balls;
    }
    return o;
}

BallOrbits computeBallOrbits(const std::vector<int>& loop) { return computeBallOrbits(soloLoop(loop)); }

int orbitBallAt(const BallOrbits& o, int juggler, int beat) {
    if (o.period <= 0 || juggler < 0 || juggler >= o.jugglers) return -1;
    const size_t si = static_cast<size_t>(juggler * o.period + modPositive(beat, o.period));
    if (o.loop.throws[si].value <= 0 || o.balls[si] <= 0) return -1;
    // A ball thrown from the orbit's first slot on beat startBeat + m*period reaches this slot
    // `offset` beats later; solve for m (the division is exact).
    const int startBeat = o.start[si] % o.period;
    const int lap = (beat - startBeat - o.offset[si]) / o.period;
    return o.base[si] + modPositive(lap, o.balls[si]);
}
