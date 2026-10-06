// pattern.cpp - see pattern.h.
#include "pattern.h"

namespace {

// Hand that throws on a given beat in async juggling: beat 0 is the right hand.
Hand asyncHandForBeat(int beat) { return beat % 2 == 0 ? Hand::Right : Hand::Left; }

}  // namespace

Pattern patternFromLoopValues(const std::vector<int>& values) {
    Pattern p;
    const int period = static_cast<int>(values.size());
    // Physical period: an odd-length loop needs two passes to come back to the same hand.
    const int storedBeats = (period % 2 == 1) ? period * 2 : period;
    for (int beat = 0; beat < storedBeats; ++beat) {
        const int value = values[static_cast<size_t>(beat % period)];
        if (value == 0) continue;  // empty hand: no event
        ThrowEvent e;
        e.juggler = 0;
        e.hand = asyncHandForBeat(beat);
        e.throwTick = beatsToTicks(beat);
        e.value = value;
        e.destJuggler = 0;
        e.destHand = (value % 2 == 0) ? e.hand : otherHand(e.hand);
        e.spins = defaultSpinCount(value);
        p.events.push_back(e);
    }
    Section s;
    s.start = 0;
    s.length = beatsToTicks(storedBeats);
    s.loopBeats = period;
    s.repeats = kRepeatForever;
    p.sections.push_back(s);
    return p;
}

int defaultSpinCount(int value) {
    if (value <= 2) return 0;
    if (value == 3) return 1;
    return value / 2;
}

Pattern patternFromSiteswap(const Siteswap& siteswap) {
    return patternFromLoopValues(siteswap.throws);
}

int loopPeriodBeats(const Pattern& pattern) {
    if (pattern.sections.empty()) return 0;
    return pattern.sections[0].loopBeats;
}

std::vector<int> loopThrowValues(const Pattern& pattern) {
    const int period = loopPeriodBeats(pattern);
    std::vector<int> values(static_cast<size_t>(period), 0);
    for (const ThrowEvent& e : pattern.events) {
        const Tick beat = e.throwTick / kTicksPerBeat;
        if (beat >= 0 && beat < period && e.throwTick % kTicksPerBeat == 0)
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
    const std::vector<int> values = loopThrowValues(pattern);
    const int period = static_cast<int>(values.size());
    for (int candidate = 1; candidate < period; ++candidate) {
        if (period % candidate != 0) continue;
        bool repeats = true;
        for (int i = candidate; i < period && repeats; ++i)
            repeats = values[static_cast<size_t>(i)] ==
                      values[static_cast<size_t>(i - candidate)];
        if (repeats) return candidate;
    }
    return period;
}

Pattern withPeriod(const Pattern& pattern, int periodBeats) {
    const std::vector<int> values = loopThrowValues(pattern);
    const int shortest = shortestPeriodBeats(pattern);
    if (shortest == 0 || periodBeats <= 0 || periodBeats % shortest != 0) return pattern;
    std::vector<int> newValues(static_cast<size_t>(periodBeats));
    for (int i = 0; i < periodBeats; ++i)
        newValues[static_cast<size_t>(i)] = values[static_cast<size_t>(i % shortest)];
    return patternFromLoopValues(newValues);
}

bool patternToSiteswap(const Pattern& pattern, std::string* text) {
    std::string out;
    for (int value : loopThrowValues(pattern)) {
        if (value < 0 || value > 35 || value == 25 || value == 33) return false;
        out += value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
    }
    *text = out;
    return true;
}
