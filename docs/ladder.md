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

The loop length in beats. The **+** button writes the loop out longer so the copies can later
be edited separately: `531` at period 6 is `531531`. **-** shortens it again. Only multiples of
the pattern's shortest period are possible (531 can be 3, 6, 9, ... beats long, but not 4).

## The siteswap box

Below the ladder is a text box for siteswap notation. Typing a valid siteswap replaces the
pattern. When the pattern is changed some other way (for example with the period control), the
text is rewritten to match. Only vanilla (asynchronous, one juggler, no multiplexes)
siteswap is supported so far; sync, multiplex and passing notation are recognized and reported
as not yet supported.
