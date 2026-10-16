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
// D: the reference distance; C: the cycle (omitted: the loop's period); G the floor grid it was
// made on (G<type>,<spacing>,<lines between mid-weight ones>,<spokes>,<spokes between>,<half
// turn>, e.g. "Gradial,0.5,4,8,0,1"); M<k>(x,z,facing) the
// marks (facing in degrees, 0 faces +Z); J<k>(...) a juggler's keyframes, each "beat:M<k>" on a
// mark or "beat:(x,z,facing)" free, with "~" for turning the long way round. Beats count from 0,
// like the links' [k]. A juggler who walks like another: J<k>(=J<leader>+<beats>) with an
// optional turn, a fraction of a circle as it was typed, "r+3/9" or "r-2/5" (+: counterclockwise
// seen from above, -: clockwise; "r1/2" a half turn): J2(=J1+24r+1/4). (Older files' turns in
// degrees, "r+90", are read as fractions.)
//
// A choreography can repeat rotated: after its `cycle` beats it comes round again with every
// juggler turned about the middle of the floor, again and again till it's back where it started
// ("C96r+1/3": 96 beats written out, played 3 times, 288 beats in all; "C96r+2/5": 5 times,
// visiting the five positions in star order). Only
// the first `cycle` beats are written out (and edited); the rest are those turned. Walk links
// follow the leader's whole path, turned copies included, so their offsets run over all of it.
#pragma once

#include "math3d.h"

#include <cstdlib>
#include <string>
#include <vector>

struct SpikeMark {
    float x = 0.0f, z = 0.0f;  // meters (at the reference distance)
    float yaw = 0.0f;          // the facing it gives: radians about +Y, 0 faces +Z, pi/2 faces +X
    std::string name;          // two characters, 0-9 and a-z ("01", "0a"), unique: what it's called
};

// Mark names: two characters, each 0-9 or a-z. New marks are named in base 36 from "01" ("01" ...
// "09", "0a" ... "0z", "10", ...), skipping names in use.
bool validMarkName(const std::string& name);
// The name for number n in base 36 ("01" for 1, "0a" for 10, "10" for 36); n from 1 to 1295.
std::string markNameFor(int n);

struct Keyframe {
    int beat = 0;           // in the cycle, from 0
    int mark = -1;          // on this spike mark (index), or -1: free, at x, z, yaw
    float x = 0.0f, z = 0.0f, yaw = 0.0f;
    bool longTurn = false;  // turn the long way round on the way here
};

// A turn about the middle of the floor: num/den of a circle, kept as it was typed (3/9 isn't
// made 1/3: it may be how someone thinks of a 9-spoke pattern). num > 0: counterclockwise seen
// from above; < 0: clockwise; a whole number of circles (0/den among them) is no turn at all.
constexpr int kMaxTurnDen = 32;  // (the radial grid's most spokes)
struct Turn {
    int num = 0, den = 1;
    bool none() const { return den <= 0 || num % den == 0; }
    // Exactly half a circle (no direction to it).
    bool half() const { return den > 0 && (2 * num) % (2 * den) == den; }
    float radians() const { return none() ? 0.0f : 2.0f * kPi * static_cast<float>(num) / static_cast<float>(den); }
    // How many turns bring it back where it started: den over what num and den share (3/9: 3).
    int repeats() const;
    // k of these turns, as a fraction of the same circle, the same way round (under a whole
    // circle: 2/5 CCW times 3 is 1/5 CCW).
    Turn times(int k) const;
    bool operator==(const Turn& o) const { return num == o.num && den == o.den; }
    bool operator!=(const Turn& o) const { return !(*this == o); }
};
// A turn as shown: "3/9 CCW (120°)", "1/2 (180°)", "none" (UTF-8).
std::string turnText(const Turn& t);
// ...short, for a label: "+3/9", "-2/5", "1/2", "" for none.
std::string turnShort(const Turn& t);

// A juggler who walks like another (a walk link): on beat b, where the leader is on beat
// b + offset, turned about the middle of the floor by `turn`, facing turned the same.
struct WalkLink {
    int leader = -1;  // -1: none (the juggler's own keyframes)
    int offset = 0;   // beats
    Turn turn;
    bool active() const { return leader >= 0; }
    float turnRadians() const { return turn.radians(); }
};

// What a juggler does after the beats written out: go on as `role`'s part (from its beat 1),
// turned. ("2r+1/4": J2's part, a quarter turn counterclockwise.)
struct Continuation {
    int role = -1;  // whose part (0-based); -1: their own
    Turn turn;
    int roleFor(int juggler) const { return role < 0 ? juggler : role; }
    bool isDefault(int juggler) const { return roleFor(juggler) == juggler && turn.none(); }
};
// As written in a cell or the file ("2r+1/4", "3", "r1/2"; "" for their own part, unturned), and
// back. False if it can't be read.
std::string continuationText(const Continuation& n, int juggler);
bool readContinuation(const std::string& text, Continuation* n);

struct Choreography {
    double reference = 0.0;  // the passing distance the positions are for (m); 0: none set
    int cycle = 0;           // beats the choreography repeats after; 0: the loop's period
    // Repeating rotated: each repeat turned `repeat` about the middle of the floor, round till
    // it's back where it started. None: it repeats as is. (Older files' "C24r1/4": every juggler's
    // continuation, unless they have one of their own; see `next`.)
    Turn repeat;
    // The walking loop: after its `cycle` beats (the beats written out), each juggler goes on as
    // some juggler's part (their "role"), from that part's beat 1, turned about the middle of the
    // floor (see Continuation, WalkLoop). Per juggler; missing: their own part, turned `repeat`.
    std::vector<Continuation> next;
    // The floor grid it was made on (what spike marks snap to), as the app writes it ("radial,
    // 0.5,4,8,0,1"; see app.cpp), so it comes back with the pattern; "" for none saved.
    std::string grid;
    std::vector<SpikeMark> marks;
    std::vector<std::vector<Keyframe>> keys;  // per juggler, sorted by beat
    std::vector<WalkLink> links;              // per juggler (missing: none)

    bool active() const { return !marks.empty(); }
    // A juggler's continuation (see `next`).
    Continuation continuation(int juggler) const;
    // How many times the written-out beats play before everyone's back where they started, on
    // their own part, unturned (1: it just repeats; see WalkLoop).
    int repeats() const;
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

// The whole choreography's length: `cycle` (the beats written out) times its repeats.
inline int choreographyFullCycle(const Choreography& c, int cycle) { return cycle * c.repeats(); }

// The walking loop worked out: each time round (phase), which part each juggler walks and how
// far turned. Phase 0 is the beats written out: everyone their own part, unturned. Phase p+1:
// juggler j walks what juggler next(j) walked in phase p, turned next(j)'s turn further. It runs
// till everyone's back on their own part unturned (at most kMaxPhases times round).
struct WalkLoop {
    static constexpr int kMaxPhases = 64;
    bool valid = true;
    std::string problem;          // why not: "J3's part is taken by J2 and J4; nobody takes J1's"
    std::vector<int> bad;         // the jugglers whose continuation is part of the problem
    int phases = 1;
    std::vector<std::vector<int>> role;    // [phase][juggler]: whose part
    std::vector<std::vector<Turn>> turn;   // [phase][juggler]: turned this much in all
};
// `jugglers`: how many there are (at least; those with continuations count too). Invalid (a part
// taken by two, or by nobody; a part that isn't a juggler's; never coming back) is one phase,
// with the problem said.
WalkLoop walkLoop(const Choreography& c, int jugglers);

// Where juggler `juggler` is at `beat` (fractional, any beat: taken round the whole
// choreography, rotated repeats and all) and which way they face, with positions scaled by
// `scale` (the distance in use / the reference). `cycle`: the beats written out. False if they
// have no keyframes.
bool choreographyPlacement(const Choreography& c, int juggler, double beat, int cycle, float scale, Vec3* position,
                           float* yaw);
// The same, from the keyframes already worked out (effectiveKeyframes), over `full` beats: for
// placing a juggler many times (their path).
bool placementFromKeyframes(const Choreography& c, const std::vector<Keyframe>& keys, int full, double beat, float scale,
                            Vec3* position, float* yaw);

// The keyframes the juggler walks by, over the whole choreography (WalkLoop's phases): in each
// phase, the keyframes of the part they walk then (theirs, or another juggler's), on beats
// phase * `cycle` on, turned (on free spots, unless unturned); or, with a walk link, the leader's
// moved round by the link (on marks only if there's no turn; else free spots). A part with no
// keyframes is standing on its own mark. `cycle`: the beats written out.
std::vector<Keyframe> effectiveKeyframes(const Choreography& c, int juggler, int cycle);

// Sets (or, with leader -1, clears) a juggler's walk link. Setting one deletes the juggler's own
// keyframes.
void setWalkLink(Choreography* c, int juggler, const WalkLink& link);

// Where a keyframe puts the juggler (unscaled): on its mark, or where it says.
void keyframeSpot(const Choreography& c, const Keyframe& k, float* x, float* z, float* yaw);

// Whether two keyframes put the juggler in the same place facing the same way (the juggler
// stays put between them: a loiter).
bool sameKeyframeSpot(const Choreography& c, const Keyframe& a, const Keyframe& b);

// The first name (from "01") no mark has.
std::string nextMarkName(const Choreography& c);
// The mark with this name (case ignored), or -1.
int findMarkNamed(const Choreography& c, const std::string& name);
// Gives every mark without a valid, unique name the next free one.
void nameMarks(Choreography* c);
// Adds a mark (named, if it isn't) and returns its index.
int addSpikeMark(Choreography* c, SpikeMark mark);
// A mark on this spot facing this way (unscaled), the one already there if there is one, else a
// new one. Every keyframe of a juggler's own is on a mark; this is how a spot becomes one.
int markForSpot(Choreography* c, float x, float z, float yaw);
// Puts every keyframe that isn't on a mark onto one (markForSpot), e.g. from an older file.
void keyframesOntoMarks(Choreography* c);

// Adds a keyframe for `juggler` (replacing one on the same beat).
void setKeyframe(Choreography* c, int juggler, const Keyframe& k);
// Removes `juggler`'s keyframe on `beat`. False if there isn't one.
bool removeKeyframe(Choreography* c, int juggler, int beat);
// Removes a spike mark nobody stands on; later marks' indexes shift down. Nobody moves: a juggler
// standing on their own mark only because they have no keyframes (J3 on mark 3) gets a keyframe
// on it first, so the renumbering doesn't move them. False, with who stands on it, if someone does
// (a keyframe on it, or a juggler with no keyframes or walk link standing on it as their own).
bool removeSpikeMark(Choreography* c, int mark, int jugglers, std::string* why);

std::string choreographyToText(const Choreography& c);
// False (and *c unchanged) if the text isn't a choreography.
bool choreographyFromText(const std::string& text, Choreography* c);
