# Multiple jugglers: design notes

Working notes from the design discussion (Ron and Claude), October 2026. This is a record of
decisions and open questions, not user documentation. Revisit freely: several choices are
explicitly "start simple, learn from using it".

## Principles

- **Good defaults:** typing `<3p 3|3p 3>` should give a sensible picture with no setup.
- **Progressive disclosure:** beginners never need the advanced features; advanced users find
  them in one predictable place (right-click a juggler).
- **Direct manipulation:** drag things on the ladder and the floor rather than fill in forms.
- **Consistency:** one convention everywhere (right hand on the right in every ladder view).
- **Undo everything:** every new action is undoable, so exploring is never risky.
- **Established notation:** use existing conventions; don't invent syntax.

## Notation

- Two-or-more-juggler patterns use Juggling Lab's passing notation: `<3p 3|3p 3>`, with
  `3p2`-style absolute targets for 3+ jugglers.
  ([Juggling Lab notation](https://jugglinglab.org/html/ssnotation.html))
- Later, with linking: passist.org's relative targets (`3p+1`, `3p-1`) and shared sequences
  ([passist notation](https://alpha.passist.org/pattern/notation)). Not in milestone 1.
- Which hand catches a pass is decided by timing (the value), not by a label. Worked examples
  from 4-count with 6 clubs (both jugglers right hand on even beats):
  - R-to-R double: passer `4p333`, receiver `3p233` (the receiver's held 2 fills the hole).
  - L-to-L double: passer `3p334p 2333` (the passer holds), receiver unchanged.
  - 5p: receiver `3p313` (a quick 1 from right to left).
  - R-to-R 6p: receiver `3p330` (juggles a hole). L-to-L 6p from the left right after a pass:
    passer `3p6p33 0333` (passer juggles the hole).
  - Rule of thumb: a pass that arrives later leaves the hole with the receiver; a pass that
    leaves earlier leaves it with the passer.
- Starting hand per juggler (Juggling Lab models this with its hand siteswap / handspec
  feature, [PDF](https://jugglinglab.org/html/HandSiteswapFeature.pdf)). Shown in plain English
  in the UI; defaults to right. Needed for feeds (feeder right, feedees left).
- Half-beat offsets (4-handed siteswaps, Prechac fractional passes): **tabled**.

## Linking (first priority after milestone 1)

Linking is expected to cut editing time at least in half and errors by more, so it comes right
after milestone 1.

- Notation for linked/relative patterns will go beyond Juggling Lab's and passist's, which is
  accepted. Pass targets are relative by default and locked with `$`, as in a spreadsheet: a
  pass written `4p:$1` always goes to J1 ("value first", as in passist's `3p:2`). Import/export
  of Juggling Lab notation stays available wherever the pattern can be written out fully.
- Worked example, the 10-club feed: feeder `4p:$2 3 4p:$3 3`; feedees `4p:$1 3 3 3`, with J3 =
  J2 two beats later. Checked by brute force: valid with the feedees offset by an odd number of
  beats from the feeder (J2 3 beats, J3 1 beat), which means the feedees' passes come on the
  feeder's odd beats, so the feedees start with their **left** hand. (The shorter shorthand
  feeder `4p:$2 4p:$3`, feedees `4p:$1 3` is also valid, but it's an 11-club feed: the feeder
  passes every throw.)

- "J2 = J1, offset N" (offset in beats, or typed in periods), live: editing one juggler edits all
  linked ones at their offsets.
- Pass targets in linked jugglers are relative (shift one juggler along per link step) unless
  **locked** to a specific juggler (like `$A$1` in a spreadsheet; e.g. feedees pass to J1).
- Covers 2-person patterns (offset 0 or 1 beat), star/feast (offsets in periods), feeds (feeder
  unlinked, feedees linked with locked targets).
- A drop whose copies would collide is refused, with the reason ("two clubs in J3's left on beat
  7"). Repeated refusals point at wrong offsets.
- The spreadsheet copy/paste comparison was an analogy for how linking behaves, not a feature
  request. Literally copy/pasting jugglers (with relative targets) into a pattern is a possible
  later feature, but a scary one. **Reminder for Claude: when we get to editing patterns by
  adding jugglers, bring this idea back up with Ron.**
- Per-beat exceptions on a linked juggler for irregular openings (e.g. the start of a feast).

## Edit suggestions (with sequence mode)

When an edit leaves the pattern broken, try single-throw changes near it and offer the valid
ones with an explanation ("J2 holds (2) on beat 1: your double arrives a beat late, so their left
would be empty on beat 3"). Preview them faintly on the ladder.

## Ladder

- **Vertical everywhere**, time flowing down; each juggler's right hand on the right (the
  juggler's own view, like reading a score). The 3D view is the audience's seat.
- Long term: a **space-time view**. Each hand is a line: X and Z from an idealized hand point
  (left and right of the juggler's floor position, by facing), time down Y. Feet planted: straight
  lines. Two layouts: real floor positions (matches choreography) or a strict cylinder (jugglers
  evenly around a circle, facing the axis). One view covers 1..N jugglers.
- Cylinder details: camera always outside; near strips seen from behind their juggler, far strips
  through the cylinder (mirrored, dimmed); passes cross the interior; labels are camera-facing
  billboards; an "unroll" option flattens it.
- Beat rings: each stretch of a ring takes its brightness from the nearest juggler's hand parity,
  blending between neighbors. Period shading in alternating bands. (Expect to revisit.)
- Time scale: aim for about 24 beats on screen.
- Expand/collapse per juggler as much as possible.
- Milestone 1 uses a flat vertical ladder (strips side by side = the unrolled cylinder); the 3D
  space-time view comes in milestone 2, once passing physics is proven. Each strip styles its own
  beat lines by its juggler's parity.

## Colors

Start with today's per-prop colors. In many passing patterns every club eventually follows the
same route (4-count, 2-count, `<4p 3|3 4p>`), so symmetry- or route-based colors collapse to one
color. Possible later modes: "follow" (neutral clubs, click to highlight a few) and "color by
route" (useful in 3-count: two clubs are always passed). Add only once we know what we want.

## Camera and selection

- Juggler numbers over heads (camera-facing).
- Click a juggler (3D, ladder or floor) to select; right-click for its menu (Link to..., Starting
  hand, Frame, Juggler's-eye view).
- Frame All (default), Frame Selected (bottom-anchored zoom as now), Juggler's-eye view (steady
  and natural gaze modes; milestone 2).

## Choreography (design draft, Oct 2026; next after the bracket links)

Jugglers walking and turning while they juggle. Agreed so far; nothing built yet.

**No separate pane.** Space is edited in the juggler pane, time on the ladder.
- Drag a juggler on the floor (the mouse ray meets the ground plane). A top view (a key or
  toolbar button swings the camera overhead) for precise placement; orbit back to watch.
- Keyframes are markers on a juggler's ladder strip at their beats: drag one up or down to
  retime it, right-click to delete. Pause on a beat and drag a juggler to set a keyframe there.

**Spike marks** (as on a stage floor): points on the floor, each with a facing vector.
- A new N-juggler choreography starts with N marks evenly round a circle, facing in: today's
  n-gon formation becomes the default marks, so nothing changes until a mark is moved.
- Clicking a mark (with a juggler and beat chosen) puts a keyframe there: the juggler stands on
  the mark, facing along its vector. Keyframes attached to a mark follow it when it moves.
- Marks give spatial references before the other jugglers are placed.

**Keyframes and movement.**
- A keyframe is (juggler, beat, floor position, facing), on a spike mark or free.
- Between keyframes: a straight walk, easing in and out (no lurch). Splines later.
- Standing still is two keyframes at the same spot ("stand until beat 9, arrive at beat 13").
- Facing turns smoothly between keyframes, the shorter way by default; a keyframe can flip it
  (turning left vs right through 180 degrees look different).
- Two layers of facing: the body follows keyframes; the upper body still twists toward whoever
  is being passed to, as now.
- No legs yet: jugglers glide. Planted feet and steps later (some patterns care how you plant
  your feet; a long way off).

**Looping or not.** A choreography either loops (Shooting Star, Cuisinart) or doesn't (blocking
out a performance; that needs pattern changes too, see *Units and sequences*). Looping first.
- In many patterns the jugglers come back permuted after one cycle, and it can take several
  cycles to get everyone back to where they started.

**Path links.** "J2 follows J1's path", with the same link syntax: `@1[k]` copies J1's throws
*and* J1's path, k beats in. Where the copy stands: each spike mark maps to the next one (an
option on the link, like `,marks+1`; name to decide), not a rotation by an angle, so it works
for any arrangement of marks. Free keyframes (off any mark) have no shifted copy: refuse them in
a linked path, or rotate about the center as a fallback (to decide).
- Throws and walking go together: in a Shooting Star the runner stops passing while walking
  (holding: `2 2 2 2` for a runner with 2 clubs), so one link has to carry both.
- Pass targets stay per juggler (`3p+2`: two jugglers on), so they keep working while people move.

**Physics.**
- Passes to a moving, turning catcher: each flight goes from where the thrower is at the throw
  to where the catcher will be at the catch.
- Which hand catches is fixed by timing (a `3p` from the right lands in the catcher's left), and
  with turning that hand can end up on the far side of the body: the same problem as the
  dropback (passing to someone behind you). Ugly animation is acceptable for now.

**Also needed.**
- A text form (spike marks, keyframes, path links) for the library and saved patterns.
- Camera framing over the whole floor the choreography uses, not just the current positions.
- The Distance tweakable scales the whole layout of marks.
- Warnings: walks faster than about 2 m/s, jugglers walking through each other, passes too long
  for their flight time; later, clubs passing close to moving bodies.
- Undo for every choreography edit.
- The same jugglers throughout (nobody enters or leaves) for now.
- Earlier ideas still open: default formations beyond the ring (facing pair, feed V, line, two
  facing lines, square, square with center); a performance as its own file type.

**Test cases.** The 9-club Shooting Star first (4 jugglers on 5 points of a star, the fifth a
"phantom"; passes go two places round; the runner walks through the gap to fill the phantom's
spot, so the hole and the runner role travel round; see
[juggling.org](http://www.juggling.org/help/passing/patterns/shooting-star.html)). Claude to work
out its siteswap from that write-up for Ron to check. Then the Cuisinart (below), then Ron's
6-person pattern.

## Units and sequences (design draft, Oct 2026; after choreography)

Non-looping progressions: a performance as a chain of units ("4-count, R2R double, 4-count, a
feed..."), instead of one looping pattern.

**States.** Siteswap's established idea: at a given beat, which upcoming beats have a prop
arriving, per juggler. 3 per juggler, steady, is the ground state `111`. Ron's "a club in each
hand and one on its way" is a state.
- A unit's precondition and postcondition don't need to be written: they're computed from its
  throws (what has to be arriving when it starts, what's still in the air when it ends).
- Chaining: end state == next start state (equality: an arrival nothing throws is a drop; a
  throw nothing arrives for is a hole).
- Example: the 4-count is the 4-beat unit `<3p 3 3 3|3p 3 3 3>`, `111` to `111`. `<4p 3|3p 2>`
  as a 2-beat unit is also `111` to `111`, and so is `<3 3|3 3>`: "R2R double in 4-count" is
  `<4p 3|3p 2>` then `<3 3|3 3>`. The double is a unit that drops into a 4-count wherever it
  starts on a pass beat.
- A state also carries: hand phase (whether the unit starts on a right-hand beat; swapped hands)
  and, with choreography, each juggler's spike mark and facing at the start and end (a "runner
  crosses" unit chains only where the formation matches). Which club is which doesn't matter for
  chaining (only for colors).

**Library, search, auto-fill.** States are nodes, units are edges.
- Index units by (jugglers, props, start state, end state); that's what to filter on.
- "What can follow this?" = units starting from this state. "Get me from this pattern to that
  one" = a shortest path between their states. With nothing suitable in the library,
  state-changing throws can be generated (as transition finders already do for solo siteswap).
- Open: how units are named, entered, browsed and shown (a row of unit chips with the state
  between them? the ladder as the concatenated timeline?); tempo changes between units.

## Layout

Start with draggable dividers between panes (remembered) and View > Reset Layout. Named presets
when there are three panes worth arranging.

## Distances

The user can set the distance between jugglers; it's saved with library patterns (`distance=`).
Range 1 m to about 5 m. The default grows with the pattern's highest throw, by about 25-50 cm per
step of throw height (proposal to confirm: 1 m + 0.4 m per beat of the highest throw value above
1, so 3s about 1.8 m, 4s about 2.2 m, 5s about 2.6 m, clamped to 1-5 m). (Earlier note from Ron:
passing 6 bags, hands are about 1 m apart, because bags should land vertically; clubs are passed
much farther apart.)

The distance control goes with tempo and dwell in the juggler pane's panel, renamed
"Tweakables" for now (a better name to come).

## Pass geometry

- **Balls/bags:** a pass is caught where a self is caught.
- **Clubs and rings:** passes are caught about 20 cm outside the shoulder (away from the body, to
  that hand's side) and about 20 cm in front of the juggler. Higher passes are caught with the
  hand higher.
- Selfs keep today's catch positions for every prop.

## Milestone 1: two-person passing (draft spec, for review)

**In:**
- Parse Juggling Lab's 2-juggler passing notation `<a b c|d e f>` (async, `p` passes, no
  multiplex/sync yet). Validity check across both jugglers; clear error messages.
- Pattern data: throw events already carry juggler/destination; add per-juggler position, facing
  and starting hand (right).
- Physics: passes fly from one juggler's hand to the other's, between throw/catch points that
  face the partner; spins from flight time as now; bodies face the partner; gaze splits between
  partner and props.
- Formation: face to face at the default distance for the current props; distance adjustable
  (tempo panel or juggler menu) and saved with patterns.
- 3D view: both jugglers, numbers over heads, click to select, Frame All / Frame Selected,
  camera framing both.
- Ladder: vertical flat ladder for 1 and 2 jugglers (right hand on the right), per-strip beat
  lines by parity, period shading, passes drawn between strips. Pattern-mode editing works across
  both jugglers (pick up/drop as now). The single-juggler ladder becomes vertical too.
- Pattern library: passing patterns in their own menus (the 2 Jugglers placeholder becomes real),
  starter set of common 2-person patterns (2-count, 3-count, 4-count, `<4p 3|3 4p>`, ...).
- Undo for pattern edits and distance changes.

**Not in milestone 1:** 3+ jugglers (since done; see below), linking, relative notation, starting-hand UI, half-beat
offsets, the 3D space-time ladder, the Floor view, juggler's-eye view, sequence mode, edit
suggestions, color modes, layout presets.

**Decided in review:** distance adjustable (1-5 m) in the renamed tempo panel ("Tweakables"),
default growing with the highest throw; pass catch geometry as in "Pass geometry" above (selfs
unchanged).

**Settled:** default distance 1 m + 0.4 m per step of the highest throw above 1, body to body
(no separate default for bags yet).

**Still open:**
- The panel's final name.

**Delivery:** in two parts. Part 1: notation, physics, both jugglers in 3D, selection and
framing, the pattern library, distance, undo for settings; the ladder pane shows a placeholder
for passing patterns. Part 2: the vertical ladder with editing across both jugglers, throw-value
labels (toolbar button and V), and a draggable divider between the ladder and 3D panes.
Same-hand throws bulge away from the middle of the juggler throwing them; a pass is identified
only by running from one strip to the other (revisit if it gets noisy).

## 3 to 6 jugglers (done after the text-box round)

- Hard cap of 6 jugglers for now (choreography will be tangle enough). Ron's own "psychotic"
  6-person pattern will be the test case for 6-person choreography.
- Notation: Juggling Lab's absolute targets (`3p2`), plus passist-style relative targets
  (`3p+1`, `3p-1`, modulo the juggler count) read now rather than waiting for linking. `3p+0`
  (or `+n`, or an absolute target to yourself) is accepted as a self, so columns can line up in
  files; Juggling Lab export must write it as a plain self. The target must follow the `p`
  directly (single digit).
- One style per pattern when the ladder rewrites the text: any relative target in the pattern
  means every pass is written relative (and a self written with `+0` keeps it while it's still
  a self, as long as the loop's shape hasn't changed); otherwise Juggling Lab's absolute form.
  Typing and file round-trips keep exactly what was written. Ladder and prop labels follow the
  same style.
- Default formation: a regular n-gon facing the middle, neighbors the passing distance apart,
  numbered clockwise seen from above (J2 on J1's left, the last juggler on J1's right). No automatic
  feed detection: formations (a straight feed vs. feeder in the middle, say) belong to
  choreography and probably won't be automatic.
- Turning: hands throw and catch most of the way toward a partner who isn't straight ahead
  (0.7 of the angle, at most ~50 degrees), clubs' spin planes with them; the upper body twists
  half as far, eased over about a beat. Nothing changes for partners straight ahead.
- Camera: for 3+ jugglers it starts behind and above J1 (about 29 degrees down), so a feed is
  seen from the feeder's side; framed to the floor and sized for the nearest jugglers. (Two
  jugglers keep the side view.)
- Ladder: per-juggler collapse (the triangle by each number), plus a collapse/expand-all
  toggle (toolbar button, C, View menu): if every strip is collapsed it expands all, otherwise it
  collapses all. Collapsed strips keep their throws (so passes show) but drop value labels. No
  horizontal scrolling yet; strips shrink to fit.
- Text box: Shift+Up/Down step a throw's target (self, then each juggler in turn, back to self).
- Pasting jugglers waits for linking (a Pandora's box of its own).

## Linking (done)

- Notation: a part `@k[i]` / `@k` is "Jk's throws, starting from Jk's throws[i]" (0-based, as an
  array index; negative counts from the end, Python-style). This replaced an earlier `@k+d`
  ("d beats later"), which read backwards to how patterns get designed ("J2 starts with that
  throw") and caused mirrored feasts; the old form is gone, not kept for input. Integer starts
  only (fractional/Prechac offsets tabled until there's user feedback; so is a symmetric
  one-sequence shortcut). Chains allowed, loops refused.
- Targets carry naturally: relative targets shift with the copy, absolute ones stay put, so no
  `$` lock is needed (absolute = locked).
- Options after commas on any part: `LRswap` swaps the juggler's hands (the left hand throws on
  beat 1). Named for not implying a start point (patterns are infinite both ways); room for more
  later (`offset(1/2)`, `tomahawk`, `backcross`...). A link copies its source's hands; `LRswap`
  on a link toggles relative to that. Odd starts swap hands by themselves.
- Storage is the shorthand everywhere (files included). Export to Juggling Lab will be a
  one-way write-out.
- Editing any linked juggler edits all (peers, not source/copy): the change goes to the part
  written out and every copy follows; refused with the reason if a copy would collide. Beat
  insert/delete with links refused for now (unlink first).
- Text kept as typed: unchanged throws keep their written target form; new ones follow the
  pattern's style.
- UI: right-click a juggler's number for Same as (juggler, start; invalid starts greyed with
  reasons) and Unlink. Linked strips labelled "= J1[3]". Dim toggle: L, toolbar rings button, View
  menu.

### The Cuisinart (a choreography test case)

A 4-person 4-count feast "put through the blender". Seen by floor spots it's an ordinary feast;
in the beats where two jugglers would have nobody to pass to, those two step forward, turn and
cross (the other pair's passes flying between them) and swap spots, so each juggler's own
partner sequence changes (J1: J4, J4, J3, ...) and there's always one juggler you never pass
to. Six people is worse. Decided: no per-spot target notation; the per-juggler siteswap plus
where each juggler walks says it all. Choreography needs jugglers walking (and turning) while
juggling, and clearance checks for clubs passing close to moving bodies.

## Sketching, beats and orbits (done after milestone 1)

- Sketches: loops with open throws ("?", our own text extension, saved as-is). A sketch never
  has two throws landing in one spot; it's only incomplete. Drawing is click-click: each click
  places a throw and the rubber band continues as that prop's next throw; it auto-stops on
  joining a drawn path or closing an orbit; Esc stops. Dropping on a spot something already
  lands in displaces that throw, which becomes the one being drawn (like siteswap editing).
  One undo step per throw. Right-click a throw: Delete throw / Delete path (path highlighted).
- Beats: right-click a beat line to add 1 or 2 beats above/below or delete 1 or 2 (two keeps
  hands; one swaps them for the rest of the loop). Applies at every repeat.
- Color by orbit (standard siteswap orbits, spots mod the period). Symmetry/role coloring
  (which would group 3-count's self clubs too) is still later.
- File > New Pattern... (a blank sketch); File > New Performance... waits for sequence mode.
- Sketches are animated live (no more "last complete pattern, dimmed"): open throws are empty
  hands that circle; props pop in / vanish with puffs of glowing smoke; while drawing, the
  jugglers preview the throw under the rubber band. Playback keeps running while drawing.
- Next idea (own round): the rubber-banded prop thrown "wild" toward where the mouse is when
  playback reaches it, landing on the floor; jugglers look at it, then (if their neck allows)
  at the camera, shaking their heads. Later still: idle animations for long-empty hands.
