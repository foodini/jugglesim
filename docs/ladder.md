# The ladder diagram

The ladder pane (top left) shows the juggling pattern over time. Time runs left to right, one
column per beat, numbered along the bottom. Each beat's line runs the full height of the
diagram so you can trace any point on a throw straight down to its beat number.

## Reading the ladder

- **Rails.** The top rail is the right hand (R), the bottom rail the left hand (L). Beat 1 is
  thrown by the right hand, and hands alternate every beat.
- **Throws.** Each throw is drawn from the beat it's thrown on to the beat it lands on, with an
  arrowhead at the landing.
  - **Odd throws** (1, 3, 5, ...) change hands, so they cross between the rails.
  - **Even throws** (2, 4, 6, ...) return to the same hand, so they arch *outside* the ladder:
    the right hand's above the top rail, the left hand's below the bottom rail. Higher throws
    arch higher.
  - **Empty beats** (0) are shown as a small hollow circle on the rail.
- **Balls.** Every ball has its own color, marker shape and dash pattern, so you can follow one
  ball through the pattern even without color. The legend under the title shows each ball's
  style. See *View > Color Vision* for palettes suited to different kinds of color vision, and
  *View > Color Vision > Preview all modes...* to compare them.

The **playhead** (a vertical line with a small cap) shows where the juggler is in the pattern;
see [juggler.md](juggler.md).

## Moving around

- **Mouse wheel** (or a sideways swipe on a trackpad) pans the ladder left and right. You can go
  back past beat 1 into beats 0, -1, -2, ...: the pattern repeats forever in both directions.
- **Ctrl + mouse wheel** zooms in and out around the mouse. When zoomed out, only every few
  beats are numbered.
- **Home**, **Ctrl+0**, the toolbar's **Reset View** button or *View > Reset Ladder View* puts
  beat 1 back at the left edge at the normal zoom. The button lights up when beat 1 is off
  screen.

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
3. **Click a new spot** for the held end: a beat's point on the rail, or the matching half of
   another throw. A held catch goes to the catch point of the throw whose *incoming* half
   you're pointing at; a held throw end goes to the throw point of the throw whose *outgoing*
   half you're pointing at. (The other halves are ignored while you're holding something, so a
   throw leaving the beat you're aiming at can't steal the drop.) A tooltip says what the throw
   will become. Whatever was already there is picked up in turn, and you keep going.
4. **The chain closes** when you put the held end into the empty spot.

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
"ghost". That's how a pattern gains or loses empty beats, e.g. editing a snake (`50505`).

**Esc**, a **right-click** or **Ctrl+Z** cancels the chain and puts everything back.

A chain that would make a loop longer than 64 beats can't be closed; the tooltip says so. Keep
going and close it somewhere that keeps the edit shorter, or cancel.

Throw values are limited to 0-35, except 25 and 33, because their letters (`p` and `x`) mean
passing and synchronous throws in siteswap notation.

## Undo and redo

**Ctrl+Z** undoes and **Ctrl+Y** (or **Ctrl+Shift+Z**) redoes, also under the *Edit* menu.
Each of these is one step: a finished edit chain on the ladder, a change of period, or a
siteswap typed into the text box (recorded when you press Enter or click away, so undoing
`531` doesn't step back through `53` and `5`). While you're typing in the text box, Ctrl+Z
undoes typing instead.

## The siteswap box

Below the ladder is a text box for siteswap notation. Typing a valid siteswap replaces the
pattern. When the pattern is changed some other way (for example with the period control), the
text is rewritten to match. Only vanilla (asynchronous, one juggler, no multiplexes)
siteswap is supported so far; sync, multiplex and passing notation are recognized and reported
as not yet supported.
