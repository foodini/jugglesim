// loop_ops.h - operations on a JugglingLoop that may be a sketch (have open throws).
//
// A sketch is a loop where some throws haven't been decided yet: their value is kOpenThrow,
// written "?" in siteswap text (our own extension: "<3p 3p 3p ? ? ?|? ? ? ? ? ?>"). A sketch
// never has two throws landing in the same spot; it's only incomplete. When every throw is
// decided it's an ordinary pattern.
//
// Spots: juggler j's hand on beat b. A loop slot is j * period + (b mod period).
#pragma once

#include "ladder_edit.h"
#include "pattern.h"
#include "siteswap.h"

#include <string>
#include <vector>

// ---- Basics

// Number of open throws ("?"s). 0 for a complete pattern.
int openThrowCount(const JugglingLoop& loop);

// The loop as siteswap text, "?" for open throws: "531", "<3p 3|3p 3>", "<3p ? 3|...>", with
// ",LRswap" after a juggler whose hands are swapped. With `form` (how the pattern was typed; see
// PatternForm), throws are written as they were typed and linked jugglers as links ("@2[3]")
// where the loop still follows the link (otherwise written out); without it, passes are written
// as Juggling Lab does ("3p2" with 3+ jugglers). Returns false if a value can't be written (25,
// 33 or over 35).
bool loopToText(const JugglingLoop& loop, std::string* text, const PatternForm* form = nullptr);

// ---- Links between jugglers ("@2[3]"; see PatternForm in siteswap.h)

// The loop with every linked juggler's throws (and hands) made again from what they copy, at
// loop.period. Jugglers that aren't links are unchanged.
JugglingLoop applyLinks(const PatternForm& form, const JugglingLoop& loop);

// An edit made on the ladder (`before` to `after`, the same jugglers; `after` at the same period
// or a multiple of it) carried to every linked juggler: whichever juggler was edited, the change
// goes to the part it copies, and every copy follows. False, with the reason, if the result isn't
// a pattern or sketch (a copy would put two props in one hand at once), if two edited copies
// disagree, or if the period changed some other way (beats added or deleted).
bool projectEdit(const PatternForm& form, const JugglingLoop& before, const JugglingLoop& after,
                 JugglingLoop* result, std::string* why);

// Makes juggler `j` a link ("Same as..."): juggler `to`'s throws, starting from to's
// throws[start] (so j's beat b is to's beat b + start). The
// new form and loop; false, with the reason, for a loop of links or a result that isn't a
// pattern or sketch. With `allowProblems`, a result with a problem (throws colliding) is made all
// the same, with the problem in *why: a pattern often takes several links to come right.
bool linkJuggler(const PatternForm& form, const JugglingLoop& loop, int j, int to, int start, PatternForm* newForm,
                 JugglingLoop* newLoop, std::string* why, bool allowProblems = false);
// The form with juggler `j` no longer a link (their throws written out in full).
PatternForm unlinkJuggler(const PatternForm& form, int j);

// Shortest period the loop repeats at (open throws count as a value of their own).
int loopShortestPeriod(const JugglingLoop& loop);
// The loop written out at `period` beats (a multiple of its shortest period; otherwise the loop
// is returned unchanged).
JugglingLoop loopWithPeriod(const JugglingLoop& loop, int period);

// For every loop slot, the slot whose throw lands there (-1 if none does).
std::vector<int> loopIncoming(const JugglingLoop& loop);

// ---- Paths and orbits

// The loop slots on the path through `slot` (following throws forward and back until the path
// closes on itself or reaches an open end). In a complete pattern that's the slot's orbit.
std::vector<int> loopPathSlots(const JugglingLoop& loop, int slot);

// Orbits (paths, in a sketch): the sets of throws that props travel round, in the usual
// siteswap sense (spots taken modulo the period, so in an odd-period loop a prop can visit a
// spot with the right hand on one repeat and the left on the next). `length` is the period;
// ids are per (juggler, beat mod length).
struct LoopOrbits {
    int length = 0;
    int count = 0;
    std::vector<int> idOfSpot;     // [juggler * length + beat], -1 for an empty hand or open throw
    std::vector<int> propsOfOrbit; // props travelling each orbit (0 in a sketch)
    int idAt(int juggler, int beat) const;
};
LoopOrbits computeLoopOrbits(const JugglingLoop& loop);

// For "color by orbit": the orbit each prop (BallOrbits ball id) travels.
std::vector<int> orbitOfEachBall(const JugglingLoop& loop, const BallOrbits& balls, const LoopOrbits& orbits);

// ---- Changing beats (applies at every repeat of the loop)

// Inserts `count` beats just before beat `beforeBeat` (any beat on the ladder). Every throw in
// the air across the insertion gets longer by `count` (for each repeat of the insertion it
// spans); every juggler gets an empty hand (0) on each new beat. Returns false, with a reason,
// if a throw would get too high to write (over 35, or 25 or 33).
bool insertBeats(const JugglingLoop& loop, int beforeBeat, int count, JugglingLoop* result, std::string* why);

// Deletes `count` beats starting at `firstBeat`. A prop landing on a deleted beat goes straight
// on to wherever that beat would have thrown it (in a sketch, if that's still open, its throw
// becomes open too). Returns false, with a reason, if the result can't be written (a value of
// 25, 33 or more than 35, or nothing left). *propsRemoved: props whose whole route was on the
// deleted beats (complete patterns only).
bool deleteBeats(const JugglingLoop& loop, int firstBeat, int count, JugglingLoop* result, std::string* why,
                 int* propsRemoved);

// ---- Sketching

// The period after which every prop is back where it started: the loop's period times the
// least common multiple of the props in each orbit (3 for the cascade "3", 6 for "531"). The
// ladder deletes throws at this period, so deleting a throw takes out just that prop's throw
// (and its copies, where the same prop throws it again), not every prop's. The loop's own
// period for a sketch, or if the result would be longer than kMaxLoopBeats.
int propCyclePeriod(const JugglingLoop& loop);

// The loop with one throw (the one made from `slot`), or a whole path, made open.
JugglingLoop deleteThrow(const JugglingLoop& loop, int slot);
JugglingLoop deletePath(const JugglingLoop& loop, int slot);

// Drawing a throw from `from` (whose throw is open) to land in `target` (absolute beats).
// Returns false if that's not a throw that can be written (value 0-35 except 25 and 33; a 0
// must stay in its own hand). Otherwise fills *result. If something already landed in target,
// that throw is made open and *displaced is set to where it was thrown from (absolute beat:
// target's beat minus its old value), so the user can find it a new home.
bool drawThrow(const JugglingLoop& loop, Slot from, Slot target, JugglingLoop* result, bool* displacedAny,
               Slot* displaced);

// The other way round: a throw that lands in `landing` (absolute beats; nothing else lands
// there) is given `from` as the spot it's thrown from. If `from` already threw a prop, that
// throw is displaced: *displacedAny is set, and *displacedLanding is where it lands (absolute),
// so the user can give it a new spot to be thrown from; its spot is now this throw's.
bool rethrowFrom(const JugglingLoop& loop, Slot from, Slot landing, JugglingLoop* result, bool* displacedAny,
                 Slot* displacedLanding);

// The throw drawing from `from` to `target` would make (for tooltips), if it's writable.
bool drawnThrowFor(Slot from, Slot target, LoopThrow* t);

// ---- The throwing loop (the siteswap grid's "next" column)
//
// After the period, each juggler can take up another juggler's throws (their part), from its
// beat 1; and so on round, till everyone's back on their own part. What's thrown follows the
// people, not the parts: a self stays with whoever throws it, and a pass written to J2 (or worked
// out from "+1" against the part being thrown) goes to whoever is walking J2's path when it
// lands (on the beat it's thrown again; see WalkPaths). Without a choreography's walking loop,
// paths are people, so a pass to J2 goes to J2.

// next[r]: the row (T1 = 0) whoever's on row r goes on to after the period (-1, or missing: its
// own). Juggler j starts on row j, so the rows they're on are j, next[j], next[next[j]], ...
struct ThrowLoopPlan {
    static constexpr int kMaxPhases = 64;
    bool valid = true;
    std::string problem;          // why not ("T2 and T3 both go on to T1; nothing goes on to T4")
    std::vector<int> bad;         // the jugglers whose next is part of the problem
    int phases = 1;               // times round the period till everyone's back on their own
    std::vector<std::vector<int>> part;  // [phase][juggler]: whose throws they make then
};
ThrowLoopPlan throwLoopPlan(const std::vector<int>& next, int jugglers);

// The choreography's walking loop, as paths: path[phase][juggler] is whose path juggler walks
// in that phase, each phase `cycle` beats. Empty (or everyone on their own): paths are people.
struct WalkPaths {
    int cycle = 0;
    std::vector<std::vector<int>> path;
    bool trivial() const;
    // Who walks path `d` on `beat` (any beat).
    int walker(int d, int beat) const;
    // Whose path juggler `j` walks on `beat`.
    int pathOf(int j, int beat) const;
};

// The longest the throwing and walking loops may take to come round together.
constexpr int kMaxPlayedBeats = 5040;

// What's thrown, beat by beat, once the loops are worked out.
struct PlayedLoop {
    JugglingLoop loop;     // every juggler, beat by beat (the written loop itself if nothing loops)
    bool expanded = false; // ...different from the written loop (it has copies to show)
    int period = 0;        // the written loop's period
    std::vector<std::vector<int>> part;  // the plan's parts: [phase][juggler]
    bool ok = true;
    std::string problem;   // what goes wrong ("J1 catches two props on beat 12; ...")
    // The throws a problem is about, {juggler, beat in loop}: two landing together, and a hand
    // with nothing to throw.
    std::vector<std::pair<int, int>> clash;
    // Whose part juggler j throws on `beat` (any beat).
    int partAt(int juggler, int beat) const;
};
// The written loop played with `plan` (valid) and `paths`. A sketch is played as written.
PlayedLoop playedLoop(const JugglingLoop& written, const ThrowLoopPlan& plan, const WalkPaths& paths);

// An edit made to a played loop (on the ladder) put back into the written loop: false, with the
// reason, if it changed anything past the written period (the loop's copies), or the period.
bool writtenFromPlayed(const PlayedLoop& before, const JugglingLoop& written, const JugglingLoop& edited,
                       const WalkPaths& paths, JugglingLoop* out, std::string* why);

// The next column as text, as saved ("2,1,,3": juggler numbers, blank for their own; "" if
// everyone's on their own), and back. False if it isn't that.
std::string throwNextText(const std::vector<int>& next);
bool readThrowNext(const std::string& text, std::vector<int>* next);
