// choreography.h - where the jugglers stand over time: spike marks and keyframes.
//
// Spike marks are points on the floor, each with a facing (as tape marks on a stage floor). A
// keyframe pins a juggler to a spot and a facing on a beat: on a spike mark (then it moves with
// the mark) or free. Between two keyframes the juggler walks in a straight line, easing in and
// out, and turns smoothly (the shorter way, unless the keyframe says to go the long way round).
// Standing still is two keyframes at the same spot. The choreography repeats every `cycle`
// beats (the loop's period unless set), so after the last keyframe the juggler walks to where
// the first one puts them, a cycle later.
//
// Positions are in meters for a passing distance of `reference`; they scale with the distance
// in use, so the Distance tweakable spreads or gathers the whole layout.
//
// The text form (for undo and saving, one word with no spaces):
//   D1.8/C20/M1(0,1.04,180)/M2(-0.99,0.32,108)/J1(0:M1,8:M2~)/J2(4:(0.5,0.2,90))
// D: the reference distance; C: the cycle (omitted: the loop's period); M<k>(x,z,facing) the
// marks (facing in degrees, 0 faces +Z); J<k>(...) a juggler's keyframes, each "beat:M<k>" on a
// mark or "beat:(x,z,facing)" free, with "~" for turning the long way round. Beats count from 0,
// like the links' [k]. A juggler who walks like another: J<k>(=J<leader>+<beats>) with an
// optional turn, "ccw1/4" or "cw1/3" (counterclockwise or clockwise seen from above, as a
// fraction of a circle): J2(=J1+24ccw1/4).
#pragma once

#include "math3d.h"

#include <string>
#include <vector>

struct SpikeMark {
    float x = 0.0f, z = 0.0f;  // meters (at the reference distance)
    float yaw = 0.0f;          // the facing it gives: radians about +Y, 0 faces +Z, pi/2 faces +X
};

struct Keyframe {
    int beat = 0;           // in the cycle, from 0
    int mark = -1;          // on this spike mark (index), or -1: free, at x, z, yaw
    float x = 0.0f, z = 0.0f, yaw = 0.0f;
    bool longTurn = false;  // turn the long way round on the way here
};

// A juggler who walks like another (a walk link): on beat b, where the leader is on beat
// b + offset, turned about the middle of the floor by turnNum/turnDen of a circle
// (counterclockwise seen from above when positive), facing turned the same.
struct WalkLink {
    int leader = -1;  // -1: none (the juggler's own keyframes)
    int offset = 0;   // beats
    int turnNum = 0, turnDen = 1;
    bool active() const { return leader >= 0; }
    float turnRadians() const { return turnDen > 0 ? 2.0f * kPi * static_cast<float>(turnNum) / static_cast<float>(turnDen) : 0.0f; }
};

struct Choreography {
    double reference = 0.0;  // the passing distance the positions are for (m); 0: none set
    int cycle = 0;           // beats the choreography repeats after; 0: the loop's period
    std::vector<SpikeMark> marks;
    std::vector<std::vector<Keyframe>> keys;  // per juggler, sorted by beat
    std::vector<WalkLink> links;              // per juggler (missing: none)

    bool active() const { return !marks.empty(); }
    bool hasKeys(int juggler) const;  // keyframes of their own
    WalkLink link(int juggler) const;
    // Whether the juggler moves by the choreography: their own keyframes, or a walk link to a
    // leader who does.
    bool walks(int juggler) const;
    // Whether `juggler`'s walking depends on `other`'s (through walk links), or is theirs.
    bool dependsOn(int juggler, int other) const;
    bool operator==(const Choreography& o) const;
    bool operator!=(const Choreography& o) const { return !(*this == o); }
};

// Where juggler `juggler` is at `beat` (fractional, any beat: taken round the cycle) and which
// way they face, with positions scaled by `scale` (the distance in use / the reference). False
// if they have no keyframes.
bool choreographyPlacement(const Choreography& c, int juggler, double beat, int cycle, float scale, Vec3* position,
                           float* yaw);

// The keyframes the juggler walks by: their own, or, with a walk link, the leader's moved
// round by the link (on marks only if there's no turn; else free spots). `cycle` as above.
std::vector<Keyframe> effectiveKeyframes(const Choreography& c, int juggler, int cycle);

// Sets (or, with leader -1, clears) a juggler's walk link. Setting one deletes the juggler's own
// keyframes.
void setWalkLink(Choreography* c, int juggler, const WalkLink& link);

// Where a keyframe puts the juggler (unscaled): on its mark, or where it says.
void keyframeSpot(const Choreography& c, const Keyframe& k, float* x, float* z, float* yaw);

// Whether two keyframes put the juggler in the same place facing the same way (the juggler
// stays put between them: a loiter).
bool sameKeyframeSpot(const Choreography& c, const Keyframe& a, const Keyframe& b);

// Adds a keyframe for `juggler` (replacing one on the same beat).
void setKeyframe(Choreography* c, int juggler, const Keyframe& k);
// Removes `juggler`'s keyframe on `beat`. False if there isn't one.
bool removeKeyframe(Choreography* c, int juggler, int beat);
// Removes a spike mark; keyframes on it become free keyframes where it was, and later marks'
// indexes shift down.
void removeSpikeMark(Choreography* c, int mark);

std::string choreographyToText(const Choreography& c);
// False (and *c unchanged) if the text isn't a choreography.
bool choreographyFromText(const std::string& text, Choreography* c);
