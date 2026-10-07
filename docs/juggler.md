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
as on the ladder).

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
- **Mouse wheel** zooms in and out. Zooming in keeps the bottom of the view where it is, so the
  juggler stays in view and the tops of the flights are what get cropped. Zoomed in closer than
  the juggler's own height, the view stays centered on the juggler.
- **Home** (with the mouse over the juggler) or *View > Reset Camera* goes back to the default
  view.

Your angle and zoom are kept relative to the automatic framing, so if you switch patterns while
looking from the side, you stay at the side and the new pattern is still framed.

## Tempo and dwell

Both are set in the panel in the top-right corner of the juggler pane:

- Drag a slider, or use its **-** and **+** buttons (or the mouse wheel over the slider) for
  fine steps: 1 BPM and 0.05 beats, or 10 BPM and 0.1 beats with **Shift**. Hold a button down
  to keep stepping.
- **Double-click** (or **Ctrl+click**) a slider to type an exact value; **Enter** sets it.
- **Reset** (or *Playback > Reset Tempo and Dwell*) goes back to 150 BPM and a dwell of 1.4.
- The arrow at the top left collapses the panel to a one-line readout ("150 BPM, dwell 1.40");
  click it again to bring the controls back. JuggleSim remembers which way you left it.

- **Tempo** is in beats per minute: one beat is one throw, alternating hands. It goes from 40 to
  420 (7 throws a second, about the fastest anyone throws accurately). The default is 150,
  which makes a 3 rise about half a meter above the hands.
- **Dwell** is how long a hand holds a ball before throwing it, in beats. Each hand throws every
  other beat, so dwell is out of 2: at 1.4 (the default) a hand holds each ball for 70% of its
  cycle and is empty for the rest. Real cascades are often around 1.3 to 1.6.

Short throws get a shorter dwell automatically, so they still spend some time in the air: a
throw's catch uses at most half its value as dwell. A 1 (a quick hand-across) is in the air for
half a beat. A **2** is a hold: the ball simply stays in the hand.

Throw heights follow from the tempo: every throw is timed to land exactly when it's due, so a
faster tempo means lower throws.

## Props

The **Props** menu chooses what's juggled: **Balls**, **Clubs** or **Rings**. Your choice is
remembered the next time you start JuggleSim. Every prop has the same color as its ball on the
ladder, and every flight leaves the same dashed trail.

- **Clubs** are modeled on a real 515 mm club: the hand holds the handle between the collar
  and the knob, and the club's center of mass (about 17 cm up from the hand) is what flies on
  the parabola. Clubs spin end over end, the top turning back toward the juggler as the club
  leaves the hand (clockwise, seen from the juggler's left), with the spin axis turned about
  7 degrees so each club leaves the hand pointing a little toward the other side. In the hand,
  a club is caught pointing forward and up, hangs from the wrist pointing down and forward
  (about 60 degrees below horizontal) at the bottom of the scoop, and is flicked up so it
  leaves the hand pointing forward, about 15 degrees above horizontal, already spinning. The
  top, collar and knob are a darker shade of the club's color.
- **Rings** are 32 cm across and held at the rim. They spin just like clubs, end over end
  about the same axis, so from the front you see them nearly edge-on. Each ring is banded in
  its ball's dash pattern (dark where the ladder line has gaps), so you can see it spin and
  tell rings apart without color. A ring whose ladder line is solid gets two narrow dark
  bands.
- **Spins.** Each throw of a club or ring makes a whole number of turns: about as many as
  a juggler would naturally give it for its flight time (at the default tempo, a single on a 3
  and a double on a 5; higher throws get more). The prop then turns at exactly the rate that
  brings it round in time for the catch. 1s go across flat, without turning; 2s are held.

## What you see

- **The juggler** faces you, so their right hand is on your left. Hands catch on the outside and
  throw from the inside. The hand motion follows the physics of each throw:
  - **Release:** the ball leaves the hand at exactly the speed and direction the hand is moving,
    and the hand follows through a little before stopping.
  - **Catch:** the hand gives with the ball, moving with part of its speed (up to what a hand can
    plausibly do), so catches look absorbed rather than abrupt.
  - **Scoop:** between catch and throw the hand dips to build up speed for the next throw. The
    dip is as deep as that throw needs (higher throws scoop deeper), curving in toward the body
    when it gets low.
  - The arms follow the hands, elbows pointing down and out, and the wrists bend to keep the
    palms level under the ball.
- **The body** helps with high throws, the way a real juggler's does:
  - **Knees and back.** An arm can only drive a throw so far. For higher throws the hips sink
    during the scoop (the knees bend, the feet stay planted, and the back leans forward a
    little) and rise again through the release. The body has weight, so it can't follow every
    throw exactly (the hips can't drop faster than gravity): a fast run of high throws settles
    into a steady crouch with a small bob, and a single high throw in a pattern gets a bigger
    dip that starts a little ahead of it.
  - **Wider throws.** The higher the throw, the further apart the hands throw and catch.
  - **Sway.** The torso shimmies toward whichever hand is driving a throw, more for higher
    throws.
  - **Fervor.** The higher the pattern, and the more its throw heights vary, the more fervent
    the juggler looks: lower stance, more bob and sway, elbows further out. A `3` looks
    relaxed; `97531` looks like work.
  - **The human limit.** A hand can accelerate a ball at about 60 m/s² over at most about
    60 cm (arms, knees and back together), which tops out at throws about 3.7 m above the
    hands. Beyond that the body language stays at its maximum, but the balls still fly as high
    as the tempo demands.
  - **The head** follows the balls, mostly the highest ones; the eyes show where the juggler is
    looking.
- **Props** have the same colors as their balls on the ladder (see *Props* above).
- **Trails** glow behind each prop in flight, fading out over about half the flight, but never
  more than 1.8 beats, so in patterns like 5, 7 or 373737 a trail ends just before the next prop
  passes the same spot. Each trail uses the same dash pattern as that ball's lines on the
  ladder, so balls can be told apart even without color (see *View > Color Vision*).
