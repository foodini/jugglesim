// The evaluator (juggle_sim.h): things that should hold for every pattern at every moment.
// Each library pattern is played with its own settings (props, tempo, dwell, distance,
// choreography) and sampled through a cycle (up to 24 beats); a few patterns are also played with every prop at
// 240 frames a second to look for props or hands that jump.
//
// Patterns with a throwing loop (a next column) aren't played yet: that structure is being
// redesigned.
//
// Set JUGGLESIM_TEST_VERBOSE=1 to print what was measured for each pattern.
#include "check.h"
#include "choreography.h"
#include "juggle_sim.h"
#include "pattern.h"
#include "siteswap.h"
#include "test_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

bool verbose() {
    static const bool on = std::getenv("JUGGLESIM_TEST_VERBOSE") != nullptr;
    return on;
}

bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Must match juggle_sim.cpp's neutralShoulder and juggler_figure.cpp's neutral pose.
Vec3 neutralShoulder(bool right) { return Vec3(right ? -0.19f : 0.19f, 1.43f, 0.0f); }
const Vec3 kNeutralHead(0.0f, 1.63f, 0.0f);
// The figure's arm: upper arm 0.30 + forearm 0.27 + half a hand 0.05 (juggler_figure.cpp). A
// palm further than this from its shoulder is somewhere the drawn hand can't get to.
constexpr float kFigureReach = 0.62f;
constexpr float kHeadRadius = 0.11f;

Vec3 toWorld(const JugglerState& j, Vec3 p) {
    const float c = std::cos(j.yaw), s = std::sin(j.yaw);
    return j.position + Vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

struct Played {
    Siteswap siteswap;
    Choreography choreography;
    JuggleParams params;
    BallOrbits orbits;
};
// A library pattern set up as the app plays it. False for sketches and anything not played here.
bool setUp(const LibraryPattern& p, Played* out) {
    out->siteswap = parseSiteswap(p.siteswap);
    if (!out->siteswap.valid || out->siteswap.sketch) return false;
    const PatternSettings& s = p.settings;
    if (s.hasThrowNext && !s.throwNext.empty()) return false;
    if (s.hasProp) out->params.prop = s.prop;
    if (s.hasTempo) out->params.bpm = s.tempo;
    if (s.hasDwell) out->params.dwellBeats = s.dwell;
    if (s.hasDistance) out->params.distance = s.distance;
    if (s.hasChoreography && choreographyFromText(s.choreography, &out->choreography))
        out->params.choreography = &out->choreography;
    out->orbits = computeBallOrbits(out->siteswap.loop);
    return true;
}

// What a sweep through a pattern found (the worst of each).
struct Sweep {
    std::string name;
    int notFinite = 0;          // positions or directions that were NaN or infinite
    float reach = 0.0f;         // furthest a palm got from its shoulder (m)
    float handToHead = 9.0f;    // nearest a palm got to its own head's center (m)
    float propToHead = 9.0f;    // nearest a prop in flight got to anyone's head's center (m)
    float propSpeed = 0.0f;     // fastest a prop moved between samples (m/s)
};

// A little under 8 a beat, so the samples drift through every part of the beat over a cycle.
constexpr double kSamplesPerBeat = 7.7;

Sweep sweep(const std::string& name, const Played& p) {
    Sweep w;
    w.name = name;
    const JugglingLoop& loop = p.siteswap.loop;
    const int cycle = std::max(choreographyCycle(loop, p.params), loop.period);
    const int beats = std::min(std::max(cycle, 8), 24);  // (some cycles are long, and the tests run on every build)
    const double step = 1.0 / kSamplesPerBeat;
    const float samplesPerSecond = static_cast<float>(kSamplesPerBeat * p.params.bpm / 60.0);
    std::map<int, Vec3> lastProp;
    // Starts a few beats in, once everything's been thrown.
    const int samples = static_cast<int>(beats * kSamplesPerBeat);
    for (int i = 0; i <= samples; ++i) {
        const double beat = 8.0 + i * step;
        const JugglerScene scene = evaluateScene(loop, p.orbits, p.params, beat);
        std::vector<Vec3> heads;
        for (const JugglerState& j : scene.jugglers) {
            for (Vec3 v : {j.position, j.palmRight, j.palmLeft, j.body.pelvisOffset})
                if (!finite(v)) ++w.notFinite;
            if (!std::isfinite(j.yaw) || !std::isfinite(j.body.lean) || !std::isfinite(j.body.roll)) ++w.notFinite;
            const Vec3 head = bodyPoint(j.body, kNeutralHead);
            heads.push_back(toWorld(j, head));
            for (int right = 0; right < 2; ++right) {
                const Vec3 palm = right ? j.palmRight : j.palmLeft;
                w.reach = std::max(w.reach, length(palm - bodyPoint(j.body, neutralShoulder(right != 0))));
                w.handToHead = std::min(w.handToHead, length(palm - head));
            }
        }
        for (const BallState& b : scene.balls) {
            if (!finite(b.center) || !finite(b.axis) || !finite(b.spinAxis)) ++w.notFinite;
            const std::map<int, Vec3>::iterator last = lastProp.find(b.ball);
            if (last != lastProp.end()) w.propSpeed = std::max(w.propSpeed, length(b.center - last->second) * samplesPerSecond);
            lastProp[b.ball] = b.center;
            if (b.inFlight)
                for (const Vec3& h : heads) w.propToHead = std::min(w.propToHead, length(b.center - h));
        }
    }
    if (verbose())
        std::printf("    %-36s reach %.3f  hand-head %.3f  prop-head %.3f  speed %5.1f\n", name.c_str(),
                    w.reach, w.handToHead, w.propToHead, w.propSpeed);
    return w;
}

// Every library pattern played, once (several tests look at the results).
const std::vector<Sweep>& librarySweeps() {
    static const std::vector<Sweep> sweeps = [] {
        std::vector<Sweep> all;
        for (const LibraryPattern& p : library()) {
            Played played;
            if (setUp(p, &played)) all.push_back(sweep(p.displayName(), played));
        }
        return all;
    }();
    return sweeps;
}

// The biggest frame-to-frame changes in velocity at 240 frames a second, of props (not counting
// the frame a prop is caught in) and of palms. A prop or hand that pops shows up here.
struct Jumps {
    float prop = 0.0f;
    float palm = 0.0f;
    double propBeat = 0.0, palmBeat = 0.0;
};
Jumps jumps(const char* siteswap, PropType prop) {
    Jumps out;
    const Siteswap s = parseSiteswap(siteswap);
    if (!s.valid) return out;
    JuggleParams params;
    params.prop = prop;
    const BallOrbits orbits = computeBallOrbits(s.loop);
    const float fps = 240.0f;
    const double step = params.bpm / 60.0 / fps;
    struct Track {
        Vec3 position, velocity;
        int samples = 0;
        bool inFlight = false;
    };
    std::map<int, Track> props, palms;
    auto follow = [&](Track& t, Vec3 position, bool countIt, float* worst, double* worstBeat, double beat) {
        if (t.samples > 0) {
            const Vec3 velocity = (position - t.position) * fps;
            if (t.samples > 1 && countIt) {
                const float jump = length(velocity - t.velocity);
                if (jump > *worst) {
                    *worst = jump;
                    *worstBeat = beat;
                }
            }
            t.velocity = velocity;
        }
        t.position = position;
        ++t.samples;
    };
    const double end = 8.0 + s.loop.period + 1.0;
    for (double beat = 8.0; beat < end; beat += step) {
        const JugglerScene scene = evaluateScene(s.loop, orbits, params, beat);
        for (size_t j = 0; j < scene.jugglers.size(); ++j) {
            follow(palms[static_cast<int>(2 * j)], scene.jugglers[j].palmRight, true, &out.palm, &out.palmBeat, beat);
            follow(palms[static_cast<int>(2 * j + 1)], scene.jugglers[j].palmLeft, true, &out.palm, &out.palmBeat, beat);
        }
        for (const BallState& b : scene.balls) {
            Track& t = props[b.ball];
            const bool caughtNow = t.inFlight && !b.inFlight;
            follow(t, b.center, !caughtNow, &out.prop, &out.propBeat, beat);
            t.inFlight = b.inFlight;
        }
    }
    if (verbose())
        std::printf("    %-22s %-7s prop jump %5.2f (beat %.3f)  palm jump %5.2f (beat %.3f)\n", siteswap,
                    propTypeKey(prop), out.prop, out.propBeat, out.palm, out.palmBeat);
    return out;
}

const PropType kAllProps[] = {PropType::Ball, PropType::Club, PropType::Ring, PropType::Hoop, PropType::Torch};
const char* const kJumpPatterns[] = {"3", "534", "7", "b97531", "<4p 3 3 3|3p 2 3 3>"};

}  // namespace

TEST_CASE("physics: the library patterns are all played") {
    // (A pattern that stops being playable here would quietly drop out of the tests below.)
    CHECK_GE(static_cast<int>(librarySweeps().size()), 80);
}

TEST_CASE("physics: library patterns stay finite") {
    for (const Sweep& w : librarySweeps()) CHECK_MESSAGE(w.notFinite == 0, w.name << ": " << w.notFinite << " NaN or infinite values");
}

TEST_CASE("physics: hands and props stay clear of heads") {
    for (const Sweep& w : librarySweeps()) {
        // A palm's center more than a head radius and a bit of hand from the head's center.
        CHECK_MESSAGE(w.handToHead > kHeadRadius + 0.05f, w.name << ": a palm came within " << w.handToHead << " m of its head's center");
        // A flying prop's center more than a head radius and a ball's radius from any head.
        CHECK_MESSAGE(w.propToHead > kHeadRadius + 0.04f, w.name << ": a prop flew within " << w.propToHead << " m of a head's center");
    }
}

TEST_CASE("physics: props don't teleport") {
    // Faster than any throw in the library goes, so a prop that moved further than this
    // between samples jumped.
    for (const Sweep& w : librarySweeps()) CHECK_MESSAGE(w.propSpeed < 30.0f, w.name << ": a prop moved at " << w.propSpeed << " m/s between samples");
}

// (The throw and catch points move with the body, so a juggler leaning back or crouching for a
// high pattern can still reach them.)
TEST_CASE("physics: palms stay within the arm's reach") {
    for (const Sweep& w : librarySweeps())
        CHECK_MESSAGE(w.reach <= kFigureReach, w.name << ": a palm got " << w.reach << " m from its shoulder (the arm reaches " << kFigureReach << ")");
}

TEST_CASE("physics: props and hands move smoothly, every prop") {
    for (const char* pattern : kJumpPatterns)
        for (PropType prop : kAllProps) {
            if (prop == PropType::Hoop) continue;  // (below)
            const Jumps j = jumps(pattern, prop);
            // At 240 fps a change of 8 m/s in one frame is about 200 g: a pop, not a throw.
            CHECK_MESSAGE(j.prop < 8.0f, pattern << " with " << propTypeKey(prop) << ": a prop's velocity jumped " << j.prop << " m/s at beat " << j.propBeat);
            CHECK_MESSAGE(j.palm < 12.0f, pattern << " with " << propTypeKey(prop) << ": a palm's velocity jumped " << j.palm << " m/s at beat " << j.palmBeat);
        }
}

// Hoops only as high as people throw them: an 11 (b97531) needs the hand going about 25 m/s at
// the release, overhead with the arm nearly straight, and nothing stops that smoothly.
TEST_CASE("physics: hoops and hands move smoothly") {
    for (const char* pattern : kJumpPatterns) {
        if (std::string(pattern) == "b97531") continue;
        const Jumps j = jumps(pattern, PropType::Hoop);
        CHECK_MESSAGE(j.prop < 8.0f, pattern << ": a hoop's velocity jumped " << j.prop << " m/s at beat " << j.propBeat);
        CHECK_MESSAGE(j.palm < 12.0f, pattern << ": a palm's velocity jumped " << j.palm << " m/s at beat " << j.palmBeat);
    }
}

namespace {
// The heights (m) at which passes are let go: the prop's center in the first sample it's in
// flight from one juggler to another.
std::vector<float> passReleaseHeights(const LibraryPattern& pattern) {
    std::vector<float> heights;
    Played p;
    if (!setUp(pattern, &p)) return heights;
    std::map<int, bool> wasInFlight;
    const double step = 1.0 / 48.0;
    for (double beat = 8.0; beat < 8.0 + 2 * p.siteswap.loop.period; beat += step) {
        const JugglerScene scene = evaluateScene(p.siteswap.loop, p.orbits, p.params, beat);
        for (const BallState& b : scene.balls) {
            if (b.inFlight && !wasInFlight[b.ball] && b.thrower != b.catcher) heights.push_back(b.center.y);
            wasInFlight[b.ball] = b.inFlight;
        }
    }
    return heights;
}
const LibraryPattern* findPattern(const std::string& name) {
    for (const LibraryPattern& p : library())
        if (p.name == name) return &p;
    return nullptr;
}
}  // namespace

TEST_CASE("physics: passes to someone behind are dropbacks, over the shoulder") {
    const LibraryPattern* backToBack = findPattern("Back-to-Back");
    REQUIRE(backToBack != nullptr);
    const std::vector<float> heights = passReleaseHeights(*backToBack);
    REQUIRE(!heights.empty());
    for (float h : heights) CHECK_MESSAGE(h > 1.5f, "Back-to-Back: a pass let go at " << h << " m");
}

TEST_CASE("physics: passes face to face are thrown from about the waist") {
    const LibraryPattern* fourCount = findPattern("4-count");
    REQUIRE(fourCount != nullptr);
    const std::vector<float> heights = passReleaseHeights(*fourCount);
    REQUIRE(!heights.empty());
    for (float h : heights) CHECK_MESSAGE(h < 1.45f, "4-count: a pass let go at " << h << " m");
}
