# The juggler

The right half of the window shows a juggler performing the current pattern in 3D.

## Playback

Press **?** (or *Help > Keyboard Shortcuts*) for a sheet of every key and mouse action.

The bar along the bottom of the juggler pane has the transport controls:

| Button | Key | Does |
|---|---|---|
| go to beat 1 | **B** | Back to the start (playing or paused, as it was), with the ladder scrolled back to beat 1 |
| step back | **Left** (**Shift+Left**: a whole beat) | Pauses and steps back 1/12 of a beat |
| play / pause | **Space** | Starts or stops the animation |
| step forward | **Right** (**Shift+Right**: a whole beat) | Pauses and steps forward 1/12 of a beat |
| Slow | **S** | Slow motion on or off, at the speed beside it (0.05 to 0.5 of normal) |

The same commands are in the **Playback** menu. The keys don't act while you're typing in the
siteswap box.

Next to the buttons is the current beat (beat 1 is the right hand's first throw, as on the
ladder). **Drag** it left or right to move through time, or **double-click** it to type the beat
to go to; either pauses. On the ladder, **click a beat number** to go to exactly that beat
(the nearest whole beat to the click), or **drag** in the beat numbers to move the playhead
smoothly; this pauses too. The slow-motion speed is
remembered; dragging its slider turns slow motion on.

Loading a pattern from the library, starting a new one, or pasting over the whole siteswap box
starts again from beat 1.

Stepping works in both directions, by any amount: the juggler's position at any moment is
worked out directly from the pattern (every flight is an exact parabola), so nothing has to be
replayed to get there.

Picking up a throw on the ladder pauses the juggler, so the pattern doesn't change under him
mid-throw. Press Space to carry on when you're done.

### The playhead

The ladder shows where the juggler is with a playhead: a horizontal line with a small cap at its
left end, moving smoothly down at the current beat. The same moment is also marked (more faintly)
every time the ladder's colors repeat: when the same prop is back in the same hand. Hands
alternate, so an odd period takes two loops; and the props on an orbit take turns, so with a
color per prop it takes as many loops as an orbit has props. In `3` that's every 6 beats, in a
4-count every 24; with colors by orbit, only the hands matter. So every copy shows the same
props in the same hands as the bright playhead.

The bright playhead is on the current beat, the same number as the beat counter under the
juggler pane: time runs on (beat 77, 78, ...) rather than going back to beat 1 each time the
pattern comes round. While playing, the ladder follows it: once it's 60% of the way down, the
ladder scrolls with it. Scrolling the ladder yourself pauses the following for a couple of
seconds.

## The camera

By default the camera frames the pattern: the bottom of the view is just below the lowest point
the hands reach, and the top is just above the highest point any ball reaches. Change the
pattern, tempo or dwell and the camera glides to the new framing, so a 3 fills the view and a
high 7 pulls the camera far back.

To look around:

- **Right-drag** in the juggler pane to orbit: left and right swing around the juggler, up and
  down tilt to look from above or below. A left-drag that starts on empty space does the same.
  (On a Mac trackpad, a two-finger click is a right-click: press with two fingers and drag.)
  While you orbit, a white **reticle** shows what you're orbiting about: a ring with a cross on
  the floor and, if the point is above the floor, a small cross there with a line down to the
  ring.
- **Mouse wheel** (a two-finger scroll on a trackpad) zooms in and out. Zooming in keeps the
  bottom of the view where it is, so the juggler stays in view and the tops of the flights are
  what get cropped. Zoomed in closer than the juggler's own height, the view stays centered on
  the juggler.
- **Middle-drag** (or **Shift+right-drag**) pans: the floor under the mouse moves with it. This
  makes the camera **free** (below).
- **A 3D mouse** (a 3Dconnexion SpaceMouse; Windows only for now) moves the camera whatever
  has the keyboard, the ladder, the siteswap box or a menu, and makes the camera **free**
  (below). You grab the stage, like an object in your hand: **spin** the cap to turn it round,
  **tilt** it toward or away from you to see it from higher or lower (never past straight down
  or up), **push** it away to move back and **pull** it toward you to move in. **Sliding** it
  sideways or up and down moves the stage
  the same way. The further you push, the faster it goes;
  *3D Mouse > Speed* (a menu that appears when one is connected) sets how fast. JuggleSim reads
  the device directly, so 3Dconnexion's settings for it don't matter, with one exception: if
  their software also sends JuggleSim keys of its own when you move the cap, set every axis
  speed in its *Advanced Settings* for JuggleSim to 0. (Mouse-wheel scrolling it sends while the
  cap moves is ignored already; *Help > 3D Mouse Diagnostics* shows what the cap is sending,
  live.)
- **Home** (with the mouse over the juggler) or *View > Reset Camera* goes back to the default
  view.

Your angle and zoom are kept relative to the automatic framing, so if you switch patterns while
looking from the side, you stay at the side and the new pattern is still framed.

### The free camera

Panning, or anything done with a 3D mouse, makes the camera **free**: it stays exactly where you put it, whatever the pattern does, and the corner of the
pane says *Free camera: Home re-frames*. Then:

- Spinning or tilting a 3D mouse's cap orbits, like a right-drag.
- Orbiting goes round a point that stays put for the whole drag: the selected juggler (at chest
  height, where they are when you start); else the juggler in the middle of the view; else the
  floor in the middle of the view, if that's on the stage (the floor the jugglers, spike marks
  and keyframes use, plus 1.5 m); else the edge of the stage where your line of sight leaves
  it. The reticle always shows it, faintly, and brighter while you orbit.
- The wheel (and pushing or pulling a 3D mouse) moves the camera along the way it's looking,
  faster the further it is from the nearest juggler, prop or spike mark, so it covers distance
  quickly but slows near things.
- **Home**, *View > Frame All*, **F**, a double-click, or **T** go back to the automatic
  framing (T to the automatic top view; T again for the usual view). Loading a pattern does too,
  unless it was saved with a camera.
- *File > Save* (or *Save As*) saves a free camera with the pattern, and loading the pattern
  puts the camera back there. Moving the camera isn't an undo step and doesn't count as an
  unsaved change.

## Pattern names and throw values

When the pattern has a name in the pattern library (yours or JuggleSim's), it's shown in the
top-left corner of the juggler pane. It's found from the siteswap however you got there, so
typing `51` shows "Shower", and so does editing your way to it on the ladder.

With throw values on (**V**, the ladder's **3p** button or *View > Throw Values*), each prop in
the air carries a small label with the throw it's on: `3`, `4p`, in a box edged in the prop's
color. A held 2 is labeled while it's held.

While you're sketching on the ladder (a pattern with throws not decided yet), the jugglers
juggle what's there: props pop into a hand with a puff of glowing smoke where nothing has been
drawn landing, and vanish in a puff where their next throw isn't decided, and hands with nothing
to do circle empty. While you draw, they show what the pattern would be if you clicked where
the rubber band is. See *Sketching* in [ladder.md](ladder.md). With color by orbit (**O**), the
props are colored by orbit, as on the ladder.

## Several jugglers

A passing pattern (see the siteswap box in [the ladder notes](ladder.md)) shows every juggler,
each with their number over their head (J1, J2, ..., as on the ladder; numbers that would
overlap, say for two jugglers in the same place, are moved apart, with a thin line down to their
juggler if they're moved far). Two jugglers face each
other, juggler 1 on the left as seen from the default camera. Three to six stand at the corners
of a regular polygon (a triangle, a square, ... a hexagon), facing the middle, neighbors the
passing distance apart. The camera starts behind and above J1, so you see the pattern from
J1's side (a feed from the feeder's, say), and the numbers go clockwise seen from above: J2 on
J1's left, the last juggler on J1's right. Looking down a little keeps the near jugglers from
hiding the far ones, and the whole formation is framed, feet and all. For other formations, and
for moving the jugglers, see *Choreography* below.

A juggler with swapped hands (`,LRswap`, see [ladder.md](ladder.md)) throws with the left hand
on beat 1.

They look at their partners when nothing needs watching and follow the high throws when it
does. A juggler passing to (or catching from) someone who isn't straight ahead, as around a
ring, reaches toward them: the hands throw and catch most of the way round toward that juggler
(with a club's spin turned to match), and the upper body twists part of the way, easing in and
out around the pass.

- Balls passed to a juggler are caught where they catch their own throws. Clubs and rings are
  caught further out, about 20 cm outside the shoulder and 20 cm in front, and higher passes
  are caught higher, the way club passers reach for a pass.
- **Click** a juggler to select them: their number turns dark-on-light and they're drawn in
  gold. Click empty space to deselect. (Clicking a juggler's number over their strip on the
  ladder does the same.)
- **Double-click** a juggler (or select one and press **F**, or use *View > Frame Selected*) to
  frame just them: their hands, what they hold and their own throws (passes leave the frame).
  Double-click empty space, press **F** with no one selected, or use *View > Frame All*, to
  frame everyone again. *Home* and
  *View > Reset Camera* also frame everyone.

## Choreography: moving the jugglers

Jugglers can walk and turn while they juggle. Where they stand is set with **spike marks** (as
on a stage floor) and **keyframes**. This is the first stage; see the design notes
([multi_juggler.md](design/multi_juggler.md)) for what's coming.

- **View > Choreography** (**M**, for Movement and spike Marks) shows the spike marks: a ring on the floor, numbered (in a bubble past the arrow's tip), with an
  arrow for the way someone standing on it faces (in pale yellow). The first time, there's one
  under each juggler, where the formation puts them, so nothing moves until you change something.
  The marks, the paths (below) and the camera's reticle lie on the floor, so the jugglers hide
  them where they're in front.
- **View > Top View** (**T**) swings the camera to look straight down, framing everything in
  use, for placing marks and jugglers. **T** again goes back. Right-drag sideways to turn the
  view round (it keeps looking straight down), and the wheel to zoom; panning or a 3D mouse
  makes the camera free, starting from the top view.
- **The Choreography panel** (under the Tweakables, in choreography mode) sets the **grid**
  spike marks snap to, drawn faintly on the floor:
  - **Square:** lines every 0.25 to 2 m; marks snap to where they cross.
  - **Radial:** rings every 0.25 to 2 m and 3 to 32 spokes from the middle; marks snap to where
    rings and spokes cross, or to the middle. One spoke points at the default camera, or, with
    **Turn half a spoke**, that direction falls between two.
  - **Triangles:** equilateral triangles with sides of 0.25 to 2 m; marks snap to their corners.
  - **None:** no grid shown; marks snap to the nearest 5 cm.

  To help count, every few lines (**Mid lines**, every 2 to 12; for the radial grid, rings) are
  drawn a little heavier, and the lines through the middle of the floor heaviest, with a dot in
  the middle. The radial grid can also make every few spokes heavier (**Mid spokes**): only counts
  that divide the spokes evenly are offered, so with 12 spokes, every 2, 3, 4 or 6.

  The grid is a setting of JuggleSim, not part of the pattern, and changing it never moves
  anything already placed.
- **Spike marks:** drag a mark's ring to move it (snapping to the grid; **Shift**: freely), and its
  arrow to turn it (in 15-degree steps; **Shift**: freely). Right-click a mark to make it
  **Face the Middle**, to **Delete** it, or to **Add Another Spike Mark Here** (on the same spot:
  turn it by its arrow); right-click the floor to **Add a Spike Mark** there. Where several marks
  share a spot, putting a juggler there lights up their arrows and numbers: click the one they
  stand on (**Esc** keeps the one they're on).
- **Keyframes** say where a juggler is on a beat. Pause on the beat first (the transport bar,
  or click the beat's number on the ladder), then either:
  - **drag the juggler** to where they should be: dropped on a mark, they stand on it, facing as
    it says; anywhere else, they stand there (snapped to the grid; **Shift**: freely) facing as
    they were; or
  - **select the juggler** and **click a mark** (its ring, if someone's standing on it).

  Either way, a small popup then asks how long they **loiter** there: click **0** to **4**, one
  of the lengths already used nearest this point in the choreography (shown after them), or
  type a number in the box and press Enter. Or just press a key: **0**-**9**, **a** = 10,
  **b** = 11 and so on, as in siteswap. A loiter of N beats is a second keyframe N beats later
  on the same spot, facing the same way. Lengths that would reach another of their keyframes
  are greyed out, and typing one flashes an error. **0** or **Esc** leaves just the arrival.
- **Between keyframes** a juggler walks in a straight line, easing in and out, and turns
  smoothly, the shorter way round. To stand still for a while, put two keyframes on the same
  spot. After their last keyframe they walk back to their first, so the whole choreography
  repeats. A juggler with one keyframe just stands there; one with none stands on their own
  mark (J1 on mark 1, and so on).
- **The choreography's length** (*Length* in the Choreography panel) is how many beats it takes
  to come round. It starts as the siteswap's period, but it's the choreography's own: editing
  the siteswap doesn't change it, and it doesn't have to match. Shortening it past some
  keyframes asks first, listing the keyframes that would be deleted. When neither length is a
  multiple of the other (a 3-beat siteswap with an 8-beat walk, say), a warning in the top-left
  of the juggler pane says how long it takes them to line up again (24 beats). **Dismiss** it if
  that's what you meant; it comes back only if something changes and it applies again.
- **On the ladder,** keyframes are diamonds on the juggler's strip, and a white line down the
  strip joins two where the juggler loiters between them (walking and turning aren't drawn).
  *View > Keyframe Marks* (**K**) labels each diamond on a spike mark with the mark ("M3").
  **Drag** a diamond up or down to
  move it to another beat; **right-click** it to delete it, or to have the juggler **Turn the Long
  Way Round** on the way to it (through more than half a turn). **Ctrl+drag** a diamond to make a
  loiter there: a copy of the keyframe, on the same spot facing the same way, as far up or down
  as you drag it. It stops short of the keyframes either side (it can reach one only if that's
  on the same spot already, which changes nothing).
- **Walk links:** to block out one juggler and have others copy them, right-click another
  juggler (their number on the ladder, or them on the stage) and choose **Walk Like...**. Pick the
  **leader**, an **offset** in beats (J2 does on each beat what the leader does that many beats
  later; the 1/2, 1/3 and 1/4 buttons are those fractions of the choreography's length), and a
  **turn** about the middle of the floor: none, or 1/2 to 1/5 of a circle, clockwise or
  counterclockwise seen from above. Changes show as you make them. A follower's diamonds on the
  ladder are the leader's, moved round (dim, not editable), and their strip says which leader
  (`J1+24ccw1/4`). Followers can follow followers. Linking a juggler who has keyframes deletes
  them (it asks first); **Unlink** writes the follower's walking out as their own keyframes, so
  nothing moves.
- **Paths:** each juggler with keyframes has their path drawn on the floor: where they walk over
  the whole cycle, back to the start. There's a point for every beat, with an arrow the way they
  face then (so you can see them turn as they go) and the beat's number; beats they stand still
  for share one point, numbered like "5-8". The points crowd together where they slow down to
  stop and spread out where they're walking fastest. Paths are dashed: the selected juggler's
  is bright cyan and drawn on top, everyone else's dim blue-gray, each with the juggler's number
  (J2) along it. While walking, a path
  runs a few centimeters to the walker's right, so two jugglers walking the same line opposite
  ways show as two lines. **Click a point** to go to that beat with that juggler selected, ready
  to drag them somewhere new for a keyframe. When the numbers get crowded, turn off *View > Path
  Beat Numbers*: then only the point under the mouse is numbered.
- A keyframe on a mark moves with the mark, so moving a mark moves everyone who stands on it.
- Passes go where the catcher will be when they catch, however they're walking or turning.
  Passing to someone behind you isn't handled well yet (the pass still arrives, but the hands
  don't look right).
- Until you change something (move, add or delete a mark, or make a keyframe), the marks are
  just the default formation and follow the **Distance** tweakable. From then on the
  choreography says where everyone stands, in meters, so Distance is greyed out; *View > Clear
  Choreography* makes it available again.
- Choreography is part of the pattern: it's saved with your patterns (*File > Save*), undoable
  like any other change, and *View > Clear Choreography* removes it.

## Tweakables

Tempo, dwell and, for passing patterns, the distance between the jugglers are set in the panel
in the top-right corner of the juggler pane ("Tweakables" is a working name):

- Drag a slider, or use its **-** and **+** buttons (or the mouse wheel over the slider) for
  fine steps: 1 BPM and 0.05 beats, or 10 BPM and 0.1 beats with **Shift**. Hold a button down
  to keep stepping.
- **Double-click** (or **Ctrl+click**) a slider to type an exact value; **Enter** sets it.
- **Reset** (or *Playback > Reset Tweakables*) goes back to 150 BPM, a dwell of 1.4 and the
  automatic distance.
- The arrow at the top left collapses the panel to a one-line readout ("150 BPM, dwell 1.40");
  click it again to bring the controls back. JuggleSim remembers which way you left it.
- Changes can be undone (**Ctrl+Z**): each adjustment is one step, recorded once you've stopped
  changing it for a moment.

- **Tempo** is in beats per minute: one beat is one throw, alternating hands. It goes from 40 to
  420 (7 throws a second, about the fastest anyone throws accurately). The default is 150,
  which makes a 3 rise about half a meter above the hands.
- **Dwell** is how long a hand holds a ball before throwing it, in beats. Each hand throws every
  other beat, so dwell is out of 2: at 1.4 (the default) a hand holds each ball for 70% of its
  cycle and is empty for the rest. Real cascades are often around 1.3 to 1.6.

Short throws get a shorter dwell automatically, so they still spend some time in the air: a
throw's catch uses at most half its value as dwell. A 1 is a hand-across: the throwing hand
carries the prop just past the middle and lets go, and the other hand takes it from right
beside it, the hands nearly touching; it's in the air for half a beat. A **2** is a hold: the ball simply stays in the hand.

Throw heights follow from the tempo: every throw is timed to land exactly when it's due, so a
faster tempo means lower throws.

- **Distance** (passing patterns only) is how far apart the jugglers stand, body to body, from
  1 to 5 m, in steps of 0.05 m (0.25 m with **Shift**). With **Auto** checked, it follows the
  pattern's highest throw: 1 m plus 0.4 m for each step of throw height above 1, so about 1.8 m
  for a pattern of 3s and 2.2 m with 4s. Moving the slider turns Auto off; checking it again
  goes back to the automatic distance. Library patterns can carry a distance. It's unavailable
  (greyed out) in a pattern with choreography, which puts the jugglers where they stand.

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
  Pause and rest the mouse on a club or ring in the air to see its throw and spin rate, in
  spins a beat (so it doesn't depend on the tempo); the ladder shows the same when you rest the
  mouse on a throw.

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
