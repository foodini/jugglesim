// timing.h - musical time for JuggleSim.
//
// Everything the user sees and edits is in beats. Internally, times are integer ticks so that
// subdivisions stay exact: kTicksPerBeat = 5040 = 2^4 * 3^2 * 5 * 7, which divides evenly by
// 1-10, 12, 14, 15 and 16 (halves, thirds, ... sevenths, ninths, sixteenths, and so on).
// Ticks are 64-bit: at 5040 per beat that's millions of years at any sane tempo.
//
// Only the simulator converts to seconds, and only through a TempoMap, so tempo changes can be
// added later without touching anything that works in beats.
#pragma once

#include <cmath>
#include <cstdint>

using Tick = std::int64_t;

constexpr Tick kTicksPerBeat = 5040;

constexpr Tick beatsToTicks(std::int64_t beats) { return beats * kTicksPerBeat; }

// Converts between ticks and seconds. Constant tempo for now; the interface is what matters:
// callers ask "how many seconds from tick 0 to tick t", so a piecewise tempo map can replace
// this without changing them.
class TempoMap {
public:
    explicit TempoMap(double beatsPerMinute = 120.0) : bpm_(beatsPerMinute) {}

    double bpm() const { return bpm_; }
    void setBpm(double beatsPerMinute) { bpm_ = beatsPerMinute; }

    double secondsAt(Tick tick) const {
        return static_cast<double>(tick) / static_cast<double>(kTicksPerBeat) * 60.0 / bpm_;
    }

    // Seconds between two ticks. With a variable tempo this is NOT (b - a) * secondsPerTick.
    double secondsBetween(Tick a, Tick b) const { return secondsAt(b) - secondsAt(a); }

    // Nearest tick to a time in seconds.
    Tick tickAt(double seconds) const {
        return static_cast<Tick>(std::llround(seconds * bpm_ / 60.0 * kTicksPerBeat));
    }

private:
    double bpm_;
};
