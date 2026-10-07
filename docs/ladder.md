# The ladder diagram

The ladder pane (top left) shows the juggling pattern over time. Time runs down the page, one
row per beat, numbered down the left edge. Each beat's line runs across the whole strip, so you
can trace any point on a throw straight across to its beat number.

## Reading the ladder

- **Columns.** Each hand is a column: the left hand on the left (L), the right hand on the
  right (R), as the juggler sees them, like reading a score. Beat 1 is thrown by the right hand,
  and hands alternate every beat. Right-hand beats have the brighter beat lines.
- **Jugglers.** In a passing pattern each juggler has a strip of their own (an L and an R
  column), side by side: J1, J2. Every juggler throws with the right hand on beat 1. Click a
  juggler's number over their strip to select them (the same selection as in the 3D view; see
  [juggler.md](juggler.md)); click it again to deselect.
- **Throws.** Each throw is drawn from the beat it's thrown on to the beat it lands on, with an
  arrowhead at the landing.
  - **Odd throws** (1, 3, 5, ...) change hands, so they cross between the juggler's columns.
  - **Even throws** (2, 4, 6, ...) return to the same hand, so they arch *outside* the juggler's
    columns: the right hand's to the right, the left hand's to the left. Higher throws arch
    wider.
  - **Passes** run from one juggler's strip to the other's. A 3p from the right hand lands in
    the partner's left; a 4p in their right.
  - **Empty beats** (0) are shown as a small hollow circle in the column.
- **Throw values.** The **3p** button on the toolbar, the **V** key or *View > Throw Values*
  labels every throw with its value, a little way along it from where it's thrown: `3`, `4p`
  (a pass). The props in the 3D view get the same labels while they're in the air. JuggleSim
  remembers the setting.
- **Balls.** Every ball has its own color, marker shape and dash pattern, so you can follow one
  ball through the pattern even without color. The legend under the title shows each ball's
  style. See *View > Color Vision* for palettes suited to different kinds of color vision, and
  *View > Color Vision > Preview all modes...* to compare them.

The **playhead** (a horizontal line with a small cap at its left end) shows where the jugglers
are in the pattern; see [juggler.md](juggler.md).

## Moving around

- **Mouse wheel** scrolls up and down through time. You can go back past beat 1 into beats 0,
  -1, -2, ...: the pattern repeats forever in both directions.
- **Ctrl + mouse wheel** zooms in and out around the mouse. The default shows about 24 beats.
  When zoomed out, only every few beats are numbered.
- **Home**, **Ctrl+0**, the toolbar's **Reset View** button or *View > Reset Ladder View* puts
  beat 1 back at the top at the normal zoom. The button lights up when beat 1 is off screen.

The divider between the ladder and the 3D view can be dragged to give either more room.
Double-click it (or use *View > Reset Layout*) to share the width equally again. JuggleSim
remembers where you left it.

Each ball keeps its color and shape wherever you scroll.

## Toolbar

The row of buttons above the diagram holds the ladder's modes and tools.

### Pattern/Sequence Mode (the repeat-sign button)

JuggleSim has two ways of looking at a juggling pattern:

- **Pattern mode** treats the pattern as one loop that repeats forever, like a musical phrase
  between repeat signs. Everything you change applies to every repeat. This is the mode for
  "show me 531" and for exploring patterns.
- **Sequence mode** (not implemented yet) will be a timeline in which every beat is accounted
  for: sections with exact repeat counts, such as `(3^3)(531)(3^3)` in Juggling Lab's notation,
  where a change can apply to a single occurrence. This is what choreography will build on.

Pattern mode is the only mode available so far.

### Period

The loop length in beats. The **+** button writes the loop out longer: `531` at period 6 is
`531531`. **-** shortens it again. Only multiples of the pattern's shortest period are possible
(531 can be 3, 6, 9, ... beats long, but not 4).

Editing on the ladder always produces a loop at least this long, so **+** is how you ask for a
longer loop than your edit would otherwise make: in `333` (period 3), closing a 3's catch six
beats past its empty spot (with Shift) gives `933`, where in `3` (period 1) it gives `9`.

## Editing the pattern on the ladder

In Pattern mode you edit by picking up one end of a throw and putting it somewhere else. This
is a *siteswap* in the literal sense: every drop exchanges where two throws land (or start), so
the result is always a valid pattern.

1. **Hover** over a throw. The half of the curve nearest the mouse lights up, strongest at its
   end, to show which end a click would pick up: near the start, the *throw*; near the end, the
   *catch*. If two curves are almost equally close, neither lights up; move a little closer to
   the one you want.
2. **Click** to pick up that end. The throw glows and follows the mouse like a rubber band. The
   spot you picked it up from is now empty and is marked with a pulsing ring.
3. **Click a new spot** for the held end: a hand's point on a beat (any juggler's), or the
   matching half of another throw. A held catch goes to the catch point of the throw whose *incoming* half
   you're pointing at; a held throw end goes to the throw point of the throw whose *outgoing*
   half you're pointing at. (The other halves are ignored while you're holding something, so a
   throw leaving the beat you're aiming at can't steal the drop.) A tooltip says what the throw
   will become. Whatever was already there is picked up in turn, and you keep going.
4. **The chain closes** when you put the held end into the empty spot.

With two jugglers, where you drop decides everything: drop a held catch in the other juggler's
column and the throw becomes a pass; drop a pass's catch back in the thrower's own columns and
it becomes a self. For example, in a 4-count (`<3p 3 3 3|3p 3 3 3>`), pick up the catch of J1's
first pass and drop it in J2's right hand on beat 5: it becomes a 4p, and J2's throw that used to
land there (the 3 from beat 2) is picked up. Drop that in the empty spot (J2's left on beat 4)
and it becomes a 2: the result is `<4p 3 3 3|3p 2 3 3>`, the right-to-right double with the
receiver's hold.

While you're editing, only the throws you touch change. When the chain closes, your edit
becomes the pattern's loop: the loop is as long as the stretch of beats you touched, rounded up
to a multiple of the current period. For example, in a `3`, pick up the catch of beat 1's throw,
drop it on beat 6 (picking up beat 3's throw), and close on beat 4: you touched beats 1-3, so
the new pattern is `531`. While the mouse is over a spot where the held end can be dropped,
faint copies show how the edit would repeat.

The pattern is then rewritten at its shortest period, and the siteswap text updates.

**Shift-click** a copy of the empty spot in another repeat to close the chain *there* instead.
That makes one throw a whole loop higher or lower, which adds or removes balls: in a `3`,
closing six beats past the empty spot gives `9`. The copies you can close on are marked with
squares (the real empty spot is a circle); they brighten while Shift is held, and the tooltip
tells you how many balls the change would add or remove. (A plain click on a copy just picks up
the throw that's there, like anywhere else.)

Empty beats (0s) can be picked up and moved like throws; while held they appear as a grey
"ghost". A 0 stays with its own juggler (it's an empty hand, not a throw). That's how a pattern gains or loses empty beats, e.g. editing a snake (`50505`).

**Esc**, a **right-click** or **Ctrl+Z** cancels the chain and puts everything back.

A chain that would make a loop longer than 64 beats can't be closed; the tooltip says so. Keep
going and close it somewhere that keeps the edit shorter, or cancel.

Throw values are limited to 0-35, except 25 and 33, because their letters (`p` and `x`) mean
passing and synchronous throws in siteswap notation.

## Adding and deleting beats

**Right-click a beat line** (away from any throw) for a menu:

- **Add a beat above / below**, **Add two beats above / below**: every throw in the air across
  the new beats gets longer by that many beats, and every juggler gets an empty hand (0) on
  each new beat.
- **Delete this beat**, **Delete this beat and the next**: a prop caught on a deleted beat goes
  straight on to wherever that beat would have thrown it, so its two throws become one. A prop
  whose whole route was on the deleted beats disappears (deleting the 3 in `531` leaves `31`).

Like every edit in Pattern mode, these apply at every repeat of the loop, and the period
changes to match. Hovering over an item shows the resulting siteswap first. Hands alternate
every beat, so adding or deleting **one** beat swaps left and right for the rest of the loop
(the tooltip says so); adding or deleting **two** keeps every throw in its hand, which is
usually what you want in a passing pattern. Something that would make a throw higher than
siteswap can write (35) is refused, with the reason.

## Sketching

A **sketch** is a pattern with throws not decided yet, shown as **?** (in the text box too:
`<3p 3p 3p ? ? ?|? ? ? ? ? ?>`). It's how you build a pattern up from nothing, the way you might
on paper.

- *File > New Pattern...* starts one: choose 1 or 2 jugglers and the period, and every throw is
  a ?. If the pattern you have has changed since you loaded, saved or started it, you're asked
  first whether to save it (changes to tempo, dwell or distance alone don't count).
- **Right-click a throw** for **Delete throw** (it becomes a ?) or **Delete path**, which does
  that to every throw on its path: the round a prop (or group of props) travels. Hovering over
  *Delete path* lights the path up first. Deleting from a finished pattern turns it into a
  sketch.

**Drawing.** Click a **?** to start a throw from there, then click where the prop is caught: any
juggler's hand on any later beat (the same spot for a 0). The value, and whether it's a pass,
come from where you click. The rubber band then carries on from the spot you clicked, as that
prop's next throw, so you can follow one prop around click by click. It stops by itself when the
prop reaches a spot whose throw is already drawn (it joins that path, or closes its own), and
**Esc** or a right-click stops it any time. Each throw drawn is one undo step.

While drawing, faint rings mark the spots nothing lands in yet. If you click a spot something
already lands in, your throw takes it, and the throw that landed there is picked up instead:
find it a new home the same way (as when editing a finished pattern).

Picking up a throw that's already drawn works as when editing a finished pattern: the half of
the curve nearest the mouse lights up, and that's the end you get. The throw is taken out (its
spot becomes a ?) to be placed again:

- **By its catch:** it's still thrown from the same spot; click where it should land, as when
  drawing.
- **By its throw:** it still lands in the same spot; click where it should be thrown from. A ?
  takes it and you're done. A spot that already throws something takes it too, and the throw
  that was there is picked up by its start in turn, to be given a new spot to be thrown from
  (Esc leaves it as a ?).

Open spots are marked so you can see what's left without color: a **?** in a dashed ring for a
throw not decided yet, and a small notch over a spot nothing lands in yet. When every throw is
decided, the sketch is an ordinary pattern: it plays, and it's written at its shortest period.

The jugglers juggle the sketch as far as it goes: a prop thrown from a spot nothing lands in
pops into the hand (with a puff of glowing smoke) when it would have been caught there, and a
prop caught in a spot whose next throw isn't decided yet vanishes in a puff when it would have
been thrown. A hand with nothing decided just circles empty, so a new, blank pattern shows the
jugglers standing there. While you're drawing, the jugglers keep going, and when the rubber
band is over a spot it can land in, they show what the pattern would be if you clicked there.
(Picking up a throw in a finished pattern still pauses them.)

Sketches can be saved to My Patterns like any pattern.

## Colors: props or orbits

Normally every prop has its own color, marker shape and dash pattern. The orbit button on the
toolbar (next to **3p**), the **O** key or *View > Color by Orbit* colors by **orbit** instead: the set of throws a group of props travels round. In `531` the 5s
and 1s form one orbit (carrying two balls) and the 3s another; in a 3-count the two clubs that
are only ever passed share an orbit, and each self club has its own; in a 4-count every club
goes everywhere, so it's all one orbit. The legend shows each orbit and how many props it
carries. The props in the 3D view follow the same setting. Sketches are always colored by path.

## Undo and redo

**Ctrl+Z** undoes and **Ctrl+Y** (or **Ctrl+Shift+Z**) redoes, also under the *Edit* menu.
Each of these is one step: a finished edit chain on the ladder, a change of period, a
siteswap typed into the text box (recorded when you press Enter or click away, so undoing
`531` doesn't step back through `53` and `5`), a pattern loaded from the library, beats added or
deleted, a throw drawn or deleted in a sketch, or a change
to the props, tempo, dwell or passing distance (recorded once you've stopped adjusting it for
a moment, so dragging a slider is one step). While you're typing in the text box, Ctrl+Z
undoes typing instead.

## The siteswap box

Below the ladder is a text box for siteswap notation. Typing a valid siteswap replaces the
pattern. When the pattern is changed some other way (for example with the period control), the
text is rewritten to match. Vanilla siteswap (asynchronous, one juggler, no multiplexes) and
two-person passing are supported so far; sync and multiplex notation, and passing for three or
more jugglers, are recognized and reported as not yet supported.

Passing patterns use [Juggling Lab's notation](https://jugglinglab.org/html/ssnotation.html):
each juggler's throws between `<` and `>`, separated by `|`, with `p` marking a pass to the
other juggler. `<3p 3|3p 3>` is a 2-count: each juggler passes every other throw. Both jugglers
throw with the right hand on beat 1, and which hand catches a pass follows from its timing, as
in any siteswap: a `3p` from the right lands in the partner's left, a `4p` in their right. The
box below the text shows the number of jugglers along with the props and period.

Passing patterns are drawn and edited on the ladder like any other (see above).

**?** is a throw not decided yet: JuggleSim's own extension, for sketches (see *Sketching*
above). Other programs won't read it.
