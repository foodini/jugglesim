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

## Choreography (later)

- 2D top-down Floor view: numbered tokens with facing arrows and L/R marks, pass lines.
- Default starting formations: facing pair, feed (V), triangle, line, two facing lines, square,
  circle, square with center, ...
- Static positions first; keyframed movement later on a timeline sharing the ladder's vertical
  time axis. Warnings for passes too long for their flight time and for collisions.
- A choreographed performance is its own file type.

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

**Not in milestone 1:** 3+ jugglers, linking, relative notation, starting-hand UI, half-beat
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
