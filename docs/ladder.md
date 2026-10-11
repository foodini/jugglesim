# The ladder diagram

The ladder pane (top left) shows the juggling pattern over time. Time runs down the page, one
row per beat, numbered down the left edge. Each beat's line runs across the whole strip, so you
can trace any point on a throw straight across to its beat number.

## Reading the ladder

- **Columns.** Each hand is a column: the left hand on the left (L), the right hand on the
  right (R), as the juggler sees them, like reading a score. Beat 1 is thrown by the right hand,
  and hands alternate every beat. Right-hand beats have the brighter beat lines.
- **Jugglers.** In a passing pattern each juggler has a strip of their own (an L and an R
  column), side by side: J1, J2, ... up to J6. Every juggler throws with the right hand on beat
  1. Click a juggler's number over their strip to select them (the same selection as in the 3D
  view; see [juggler.md](juggler.md)); click it again to deselect.
- **Linked jugglers** (`@2[3]`, see *The siteswap box*) say what they copy under their number;
  right-click a juggler's number for *Same as* and *Unlink*. **L** dims linked jugglers' throws.
- **Choreography keyframes** (where a juggler stands on a beat; see *Choreography* in
  [juggler.md](juggler.md)) are diamonds on the juggler's strip: drag one up or down to move it
  to another beat, right-click it to delete it or to turn the long way round.
- **Collapsing strips.** With several jugglers it gets busy. The small triangle button just
  left of a juggler's number collapses their strip to a narrow one (or expands it again); their
  throws are still drawn, so passes to and from them still show, but without value labels. The
  toolbar's arrows button, **C** or *View > Collapse All Jugglers* collapses every strip, or
  expands them all if they're all collapsed already. **Ctrl+1** to **Ctrl+6** collapse or expand
  one juggler's strip.
- **Resting the mouse on a throw** says what it is ("J2, beat 6: 3"); with clubs or rings, also
  its spin ("a double, 0.45 spins a beat").
- **Throws.** Each throw is drawn from the beat it's thrown on to the beat it lands on, with an
  arrowhead at the landing.
  - **Odd throws** (1, 3, 5, ...) change hands, so they cross between the juggler's columns.
  - **Even throws** (2, 4, 6, ...) return to the same hand, so they arch *outside* the juggler's
    columns: the right hand's to the right, the left hand's to the left. Higher throws arch
    wider.
  - **Passes** run from one juggler's strip to another's. A 3p from the right hand lands in
    the partner's left; a 4p in their right. With three or more jugglers, a pass's label says
    who it goes to (`3p2`, or `3p+1`; see *The siteswap box*).
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
  **Home** and **Ctrl+0** (with the mouse over the ladder) take the playhead back to beat 1 too,
  like **B**.

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

- *File > New Pattern...* starts one: choose 1 to 6 jugglers and the period, and every throw is
  a ?. If the pattern you have has changed since you loaded, saved or started it, you're asked
  first whether to save it (changes to tempo, dwell or distance alone don't count).
- **Right-click a throw** for **Delete throw** (it becomes a ?) or **Delete path**, which does
  that to every throw on its path: the round a prop (or group of props) travels. Hovering over
  *Delete path* lights the path up first. Deleting from a finished pattern turns it into a
  sketch, written out as long as it takes every prop to get back where it started: deleting
  one throw of the cascade `3` gives `?33`, taking out just that prop's throw (and its copies,
  where the same prop throws it again), not every prop's. Likewise *Delete path* takes out one
  prop's route.

**Drawing.** Click a **?** to start a throw from there, then click where the prop is caught: any
juggler's hand on any later beat (the same spot for a 0). The value, and whether it's a pass,
come from where you click. The rubber band then carries on from the spot you clicked, as that
prop's next throw, so you can follow one prop around click by click. It stops by itself when the
prop reaches a spot whose throw is already drawn (it joins that path, or closes its own), and
**Esc** or a right-click stops it any time. Each throw drawn is one undo step.

While drawing, faint rings mark the spots nothing lands in yet. If you click a spot something
already lands in, your throw takes it, and the throw that landed there is picked up instead:
find it a new home the same way (as when editing a finished pattern).

A spot that throws something but that nothing lands in yet:
click right on it to draw a catch into it. The rubber band runs from the mouse to that spot;
click where the prop is thrown from, with the same rules as holding a throw by its start
(below).

Picking up a throw that's already drawn works as when editing a finished pattern: the half of
the curve nearest the mouse lights up, and that's the end you get. The throw is taken out (its
spot becomes a ?) to be placed again:

- **By its catch:** it's still thrown from the same spot; click where it should land, as when
  drawing.
- **By its throw:** it still lands in the same spot; click where it should be thrown from. A ?
  takes it and you're done. A spot that already throws something takes it too, and the throw
  that was there is picked up by its start in turn, to be given a new spot to be thrown from
  (Esc leaves it as a ?).

A throw not decided yet is a **?** in a large dashed ring, so you can see what's left without
color. When every throw is decided, the sketch is an ordinary pattern: it plays, and it's
written at its shortest period.

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

Along the bottom of the window, the full width, is a text box for siteswap notation. Typing a
valid siteswap replaces the pattern. When the pattern is changed some other way (for example
with the period control), the text is rewritten to match. Vanilla siteswap (asynchronous, one juggler, no multiplexes) and
passing for up to 6 jugglers are supported so far; sync and multiplex notation are recognized
and reported as not yet supported.

Passing patterns use [Juggling Lab's notation](https://jugglinglab.org/html/ssnotation.html):
each juggler's throws between `<` and `>`, separated by `|`, with `p` marking a pass to the
other juggler. `<3p 3|3p 3>` is a 2-count: each juggler passes every other throw. Both jugglers
throw with the right hand on beat 1, and which hand catches a pass follows from its timing, as
in any siteswap: a `3p` from the right lands in the partner's left, a `4p` in their right. The
box below the text shows the number of jugglers along with the props and period.

With three or more jugglers, a pass says who it goes to, right after the `p`:

- **Juggling Lab's way, by number:** `3p2` is a pass to J2. `<3p2 3|3p3 3|3p1 3>` is a
  2-count around a triangle: J1 passes to J2, J2 to J3, J3 to J1.
- **Relative (as passist.org writes them):** `3p+1` is a pass to the next juggler, `3p-1` to the
  one before, wrapping round (in a 5-person pattern, J4's `3p+4` goes to J3). The same triangle
  is `<3p+1 3|3p+1 3|3p+1 3>`. Relative targets work with two jugglers too.
- `3p+0` (or `3p+5` with five jugglers, or `3p1` from J1) is a pass to yourself, which is just
  a self. It's allowed so the columns of a pattern can line up in a file; programs other than
  JuggleSim may not accept it.

The target has to follow the `p` directly: `3p2 3` is a pass to J2 and then a 3, while `3p 2`
is a pass with no target (an error, with three or more jugglers) followed by a 2.

When the ladder rewrites the text (after an edit there), every throw you didn't change is
written exactly as you typed it (`3p+1`, `3p-4`, `3p2`, a `+0` on a self), and links stay links.
A new or changed pass is written the way the pattern already writes passes: relative if it uses
any relative targets (with `-` if they were all written that way), otherwise by number, as
Juggling Lab does. The ladder's throw labels and the labels over the props follow the pattern's
style too.

### Linked jugglers

A juggler's part can be a **link** instead of throws: `@2[3]` means "J2's throws, starting 3
beats in". On beat 1 this juggler throws J2's 4th throw, on beat 2 J2's 5th, and so on round
(after J2's last throw comes J2's first). The number in brackets is how many beats after J2's
first throw the copy starts: `@2[0]`, written just `@2`, is the same as J2 on the same beats;
`@2[1]` starts with J2's second throw. Programmers will recognize it as an array index counting
from 0, and as in Python, a negative number counts back from the end: `@2[-1]` starts with J2's
last throw. (It's written back counting from the start: with 5 throws, `[-1]` becomes `[4]`.)

The line under the siteswap box shows the number to use: with the cursor at a throw it says, for
example, "J1, beat 5 [4]", and **Ctrl+click** writes the link for you (see *Help while typing*).

A one-count feast for five, each juggler a copy of the one before, starting 3 beats in:

    <3p+1 3p+2 3p+3 3p+4 3|@1[3]|@2[3]|@3[3]|@4[3]>

and turning every `+` into a `-`, in the targets and in the brackets, reverses it:
`<3p-1 3p-2 3p-3 3p-4 3|@1[-3]|@2[-3]|@3[-3]|@4[-3]>` (written `@1[2]|@2[2]|@3[2]|@4[2]`).
A link keeps each throw's target the way it was written, which is what makes this work:

- A **relative** target shifts with the copy: J1's `3p+1` goes to J2, and in J3 (a copy of J1)
  the same throw goes to J4.
- An **absolute** target stays put: if J2's part has `4p1`, any juggler copying J2 passes to J1
  too (handy for feeds).

Links can copy links (`@3[2]` where J3 is `@1[1]` starts with J1's throws[3]), but not in a
loop, and at least one juggler has to have their throws written out.

**Editing a linked juggler edits all of them.** Change a throw on the ladder, in the juggler
written out or in any copy, and the change goes to the part written out and from there to every
copy. If a copy would then collide (two props in one hand at once), the edit is refused and the
line under the siteswap box says why. Adding or deleting beats isn't possible with links yet:
unlink first.

On the ladder, a linked juggler's strip says what it copies under their number (`= J1[3]`).
**Right-click a juggler's number** for **Same as** (pick a juggler and the beat to start from;
starts that wouldn't make a pattern are greyed out, saying why) and **Unlink** (write their throws out in
full, to edit on their own). The toolbar's rings button, **L** or *View > Dim Linked Jugglers*
mutes the linked jugglers' throws, so the parts written out stand out.

### Swapped hands

`,LRswap` after a part swaps that juggler's hands: their left hand throws on beat 1. It works on
any part, linked or not, and on a solo pattern (`531,LRswap`). A link copies the hands of the
juggler it copies, and `,LRswap` on the link swaps them again. Starting an odd number of beats
in swaps hands by itself, since every throw moves onto the other hand's beat. So in the
7-club 2-count, `<4p 3|@1[1]>` has J2 pass from the left hand, and `<4p 3|@1[1],LRswap>` has
both jugglers pass from the right.

Passing patterns are drawn and edited on the ladder like any other (see above).

**?** is a throw not decided yet: JuggleSim's own extension, for sketches (see *Sketching*
above). Other programs won't read it.

### Help while typing

- **Autofill.** Typing `<` in front of a solo pattern makes it a passing pattern with a second
  juggler still to decide: `531` becomes `<5 3 1|? ? ?>`. `<3` becomes `<3|?>`, and each throw
  you add to one juggler's part adds a `?` at the same place in the other's (`<5 3 1|5 3 1>`
  with a `7` typed at the front becomes `<7 5 3 1|? 5 3 1>`). Deleting a real throw also
  deletes the `?`s at the same place in the other parts; deleting a `?` deletes just that `?`,
  and never touches anyone else's throws. If the very next thing you do after deleting a real
  throw is type a throw in the same place, you were replacing it, and the `?`s come back:
  backspacing the 7 in `<7 5 3 1|? 5 3 1>` and typing `8` gives `<8 5 3 1|? 5 3 1>`. (Moving the
  cursor first, or any other edit, keeps the deletion.) A `|` typed at the end adds a juggler,
  all `?`, and so does a pass to a juggler the pattern doesn't have yet: `<3p+2` becomes
  `<3p+2|?|?>`. Typing `@` at the start of a part of `?`s replaces them with a link. A missing
  `>` is added.
  Passing patterns are written in the standard style: spaces between throws, none around the
  bars.
- **Up/Down** change the throw at the cursor: `?`, then 0, 1, 2, … up to 35 (z), skipping 25 and
  33 (whose letters, p and x, mean other things). A 0 can't be a pass, so it loses its `p`.
- **Shift+Up/Down** change where the throw at the cursor goes: a self becomes a pass to the
  next juggler (Up) or the one before (Down), then the one after that, and so on round to a self
  again. It's written in the pattern's style (`3p2`, or `3p+1` if the pattern uses relative
  targets; a bare `p` with two jugglers).
- **Ctrl+T** (**Cmd+T** on a Mac) swaps where two throws land — the *siteswap* operation that
  turns one pattern into another. Select two throws in one juggler's part, or just put the
  cursor after them: `531` with `31` selected becomes `522`. (Throws `d` beats apart swap
  landings by trading `d` between them: the first becomes the second's value + `d`, the second
  the first's − `d`.) Doing it again swaps them back. The pattern repeats, so with the cursor
  just after a part's first throw, it swaps with the part's last throw (the one before it, in
  the repeat before): `531` becomes `036`, and `<3p 3 3 3|3p 3 3 3>` becomes
  `<2 3 3 4p|3p 3 3 3>`. At the very start of a part, the last two throws swap. If it can't be done (a `?`, or a throw that would go below 0), the line
  under the box says why.
- **Ctrl+R** and **Ctrl+L** (**Cmd+R** / **Cmd+L** on a Mac) rotate the pattern right or left:
  the same pattern, started a beat earlier or later. `7531` becomes `1753` (right) or `5317`
  (left). Every juggler's part rotates together: `<3p 5 3 1|3p 5 3 1>` becomes
  `<1 3p 5 3|1 3p 5 3>`. Rotating by an odd number of beats swaps which hand starts.
- **Ctrl+click** (**Cmd+click** on a Mac) a throw in another juggler's part to make the part
  the cursor is in a copy of that juggler, starting with the throw you clicked. With the cursor
  in J2's part, clicking J1's throw on beat 9 makes J2 `@1[8]`: on beat 1, J2 throws what J1
  throws on beat 9. Whatever J2's part held (throws, `?`s, another link) is replaced; a
  `,LRswap` stays. The cursor doesn't move, so you can click again to try another start.
- **Where the cursor is** shows at the right of the line under the box while you type: the
  juggler, beat (with the number a link to it would use) and hand of the throw the cursor is in
  or just after ("J1, beat 5 [4], right hand: 3p+2"), or what the link there means ("J2: J1's
  throws, starting 8 beats in (from J1's beat 9)").
- **Pointing at a throw** describes it ("J1, beat 3, right hand: 4p, a pass to J2's right
  hand, caught on beat 7") and picks it out on the ladder at every repeat.
- **Mistakes are underlined**: when two throws land in the same hand at the same time, both
  get a zigzag underline, along with the message below the box.
- **Enter** (or clicking away) tidies the text into the standard form.
