# The juggler

The right half of the window shows a juggler performing the current pattern in 3D.

## Playback

The bar along the bottom of the juggler pane has the transport controls:

| Button | Key | Does |
|---|---|---|
| step back | **Left** (**Shift+Left**: a whole beat) | Pauses and steps back 1/12 of a beat |
| play / pause | **Space** | Starts or stops the animation |
| step forward | **Right** (**Shift+Right**: a whole beat) | Pauses and steps forward 1/12 of a beat |

The same commands are in the **Playback** menu. The keys don't act while you're typing in the
siteswap box. Next to the buttons is the current beat (beat 1 is the right hand's first throw,
as on the ladder), the tempo and the dwell.

Stepping works in both directions, by any amount: the juggler's position at any moment is
worked out directly from the pattern (every flight is an exact parabola), so nothing has to be
replayed to get there.

Picking up a throw on the ladder pauses the juggler, so the pattern doesn't change under him
mid-throw. Press Space to carry on when you're done.

### The playhead

The ladder shows where the juggler is with a playhead: a vertical line with a small cap on top,
moving smoothly at the current beat. Because the pattern repeats, the same moment is also marked
(more faintly) in every other repeat on screen; the left-most one is bright.

## The camera

By default the camera frames the pattern: the bottom of the view is just below the lowest point
the hands reach, and the top is just above the highest point any ball reaches. Change the
pattern, tempo or dwell and the camera glides to the new framing, so a 3 fills the view and a
high 7 pulls the camera far back.

To look around:

- **Drag** in the juggler pane to orbit: left and right swing around the juggler, up and down
  tilt to look from above or below.
- **Mouse wheel** zooms in and out.
- **Home** (with the mouse over the juggler) or *View > Reset Camera* goes back to the default
  view.

Your angle and zoom are kept relative to the automatic framing, so if you switch patterns while
looking from the side, you stay at the side and the new pattern is still framed.

## Tempo and dwell

Both are in the **Playback** menu.

- **Tempo** is in beats per minute: one beat is one throw, alternating hands. The default is 150,
  which makes a 3 rise about half a meter above the hands.
- **Dwell** is how long a hand holds a ball before throwing it, in beats. Each hand throws every
  other beat, so dwell is out of 2: at 1.4 (the default) a hand holds each ball for 70% of its
  cycle and is empty for the rest. Real cascades are often around 1.3 to 1.6.

Short throws get a shorter dwell automatically, so they still spend some time in the air: a
throw's catch uses at most half its value as dwell. A 1 (a quick hand-across) is in the air for
half a beat. A **2** is a hold: the ball simply stays in the hand.

Throw heights follow from the tempo: every throw is timed to land exactly when it's due, so a
faster tempo means lower throws.

## What you see

- **The juggler** faces you, so their right hand is on your left. Hands catch on the outside and
  throw from the inside. The hand motion follows the physics of each throw:
  - **Release:** the ball leaves the hand at exactly the speed and direction the hand is moving,
    and the hand follows through a little before stopping.
  - **Catch:** the hand gives with the ball, moving with part of its speed (up to what a hand can
    plausibly do), so catches look absorbed rather than abrupt.
  - **Scoop:** between catch and throw the hand dips to build up speed for the next throw. The
    dip is as deep as that throw needs (higher throws scoop deeper), curving in toward the body
    when it gets low. For now the arms do all the work; knees, back and body sway for high throws
    are planned.
  - The arms follow the hands, elbows pointing down and out, and the wrists bend to keep the
    palms level under the ball.
- **Balls** have the same colors as on the ladder.
- **Trails** glow behind each ball in flight, fading out over about half the flight. Each trail
  uses the same dash pattern as that ball's lines on the ladder, so balls can be told apart even
  without color (see *View > Color Vision*).
