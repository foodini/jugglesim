// ladder_view.h - ladder diagram of a pattern, drawn with ImGui's draw list.
//
// Time runs down the page. Each juggler has a strip with two columns, the left hand on the left
// and the right hand on the right (the juggler's own view, like reading a score), and the
// strips sit side by side: J1, J2, ... Every juggler throws with the right hand on beat 1. Each
// throw is a curve from where it's thrown to where it lands, styled (color + marker shape +
// dash pattern) by which prop it carries. A pass is a curve from one strip to another.
//
// The loop shown may be a sketch (some throws still open, "?"): then open spots are marked,
// throws are drawn from them by clicking where the prop lands (and then where it goes next,
// until Esc), and props are colored by path.
//
// Right-click a beat line to add or delete beats, or a throw to delete it or its whole path.
//
// The canvas is an ImGui InvisibleButton, so editing can hit-test it.
//
// Above the diagram is a toolbar row for ladder modes and tools. It doesn't change anything
// itself: it returns what the user asked for, and the caller applies it.
#pragma once

#include "color_vision.h"
#include "ladder_edit.h"
#include "pattern.h"

#include <array>
#include <string>
#include <vector>

struct ThrowSpin;  // juggle_sim.h

// Zoom limits for the ladder (1 = default beat spacing).
constexpr float kMinLadderZoom = 0.3f;
constexpr float kMaxLadderZoom = 4.0f;

// Editing and view state that persists between frames. Owned by the caller.
struct LadderEditState {
    EditChain chain;               // the open edit chain, if any
    int heldBall = -1;             // ball id of the held throw (-1 = an empty beat)
    bool hovering = false;         // idle: a throw is highlighted...
    Slot hoverSlot;                // ...the one thrown from here
    HeldEnd hoverEnd = HeldEnd::Arrival;
    bool targeting = false;        // holding: a drop target is highlighted...
    Slot hoverTarget;              // ...this one

    // Drawing in a sketch. Normally the rubber band runs from drawFrom (an open throw) to where
    // the prop should land. With drawEnd == Departure, a throw was picked up by its start: it
    // still lands in drawTo, and the rubber band runs from the spot it should be thrown from.
    bool drawing = false;
    HeldEnd drawEnd = HeldEnd::Arrival;
    Slot drawFrom;
    Slot drawTo;

    // The right-click menu: what it was opened on.
    enum class Context { None, Beat, Throw, Juggler, Keyframe };
    Context context = Context::None;
    int contextBeat = 0;            // Beat: the beat line clicked
    int contextJuggler = 0;         // Juggler: whose number (strip header) was right-clicked; Keyframe: whose
    int contextKeyBeat = 0;         // Keyframe: its beat (in the choreography's cycle)
    // Dragging a keyframe marker up or down the ladder to retime it: whose, from which beat (in
    // the cycle), and the marker's beat on screen when the drag started (keyDragJuggler -1: none).
    int keyDragJuggler = -1, keyDragBeat = 0, keyDragScreenBeat = 0;
    // ...or, Ctrl+dragged: a copy of it on the same spot (a loiter between them), kept between the
    // neighboring keyframes: keyDragMin..keyDragMax (on screen).
    bool keyDragLoiter = false;
    int keyDragMin = 0, keyDragMax = 0;
    Slot contextSlot;               // Throw: where the throw clicked is thrown from
    std::vector<int> pathHighlight; // slots to highlight (hovering "Delete path"): juggler * period + beat,
    int pathHighlightPeriod = 1;    // with this period (the props' cycle, which may be longer than the loop's)

    // View: pan and zoom.
    float firstBeat = 0.0f;        // beat drawn at the top of the ladder (may be negative)
    float zoom = 1.0f;
    bool beatOneVisible = true;    // as of the last frame drawn
    bool scrubbing = false;        // dragging the playhead in the beat-number column
    // Following the playhead while playing (see LadderViewOptions::followPlayhead).
    double followPausedUntil = 0.0;  // the user scrolled the ladder: don't follow until this time
    // The throw the mouse is resting on (for its tooltip): juggler, beat, and since when.
    int restJuggler = -1, restBeat = 0;
    double restSince = 0.0;

    // Each juggler's strip can be collapsed to a narrow one (its throws still drawn, so passes
    // to and from it still show), to make room when there are many jugglers.
    std::array<bool, kMaxJugglers> collapsed{};
};

// True if every one of the first `jugglers` strips is collapsed.
bool allStripsCollapsed(const LadderEditState& edit, int jugglers);
// Collapse All, or Expand All if every strip is collapsed already (C, or the toolbar button).
void toggleCollapseAll(LadderEditState& edit, int jugglers);

struct LadderToolbarRequest {
    int newPeriodBeats = 0;  // 0 = no change
    bool resetView = false;
    bool toggleValues = false;  // the throw-values button was clicked
    bool toggleOrbits = false;  // the color-by-orbit button was clicked
    bool toggleCollapseAll = false;  // the collapse/expand-all button was clicked
    bool toggleDimLinked = false;    // the dim-linked-jugglers button was clicked
};

// Draws the toolbar row (mode toggle, throw values, color by orbit, period control, reset
// view). Call before drawLadderDiagram.
// hasLinks: the pattern has linked jugglers (shows the dim-linked button, pressed in while
// dimLinked).
LadderToolbarRequest drawLadderToolbar(const JugglingLoop& loop, const LadderEditState& edit, bool showValues,
                                       bool colorByOrbit, bool hasLinks = false, bool dimLinked = false);

// A choreography keyframe, shown as a marker on its juggler's strip (see choreography.h).
struct LadderKeyframe {
    int juggler = 0;
    int beat = 0;       // in the cycle
    std::string where;  // for its tooltip: "mark 3", "a free spot"
    bool longTurn = false;  // turns the long way round on the way to it
    int mark = -1;          // the spike mark it's on (from 0), or -1: a free spot
    int stayBeats = 0;      // >0: the juggler stays put (same spot, same facing) this many beats,
                            // until their next keyframe (a loiter)
    bool derived = false;   // a leader's keyframe, moved round by a walk link (shown dim, not edited)
};

// How to draw the ladder (things owned by the rest of the app).
struct LadderViewOptions {
    bool showValues = false;    // label every throw with its value ("3", "4p")
    bool showKeyframeMarks = false;  // label keyframes on spike marks with the mark ("M3")
    // Walk links (choreography), per juggler: what their strip says ("walks like J1 +24 ccw1/4"),
    // empty if none; and whether a juggler's menu offers Walk Like... at all.
    std::vector<std::string> walkLabels;
    bool walkLinks = false;
    bool colorByOrbit = false;  // one color per orbit instead of per prop
    int selectedJuggler = -1;   // shown highlighted in the strip headers (-1: none)
    // A throw picked out from elsewhere (hovering it in the siteswap text box), at every repeat:
    // juggler * period + beat in the loop (-1: none).
    int highlightThrow = -1;
    bool relativeTargets = false;  // label passes "3p+1" (the pattern's text does) rather than "3p2"
    // How the pattern was typed: which jugglers are links ("@2[3]"), for their strip headers, the
    // Same as.../Unlink menu and dimming. May be null.
    const PatternForm* form = nullptr;
    bool dimLinked = false;  // draw linked jugglers' throws muted, so the parts written out stand out
    // Choreography keyframes, drawn at every repeat of the choreography's cycle.
    std::vector<LadderKeyframe> keyframes;
    int keyframeCycle = 0;
    // Scroll to keep the bright playhead on screen (while playing): the view glides down with
    // it, and when it comes round to the copy above beat 1 the view goes back by the same
    // amount, so the picture stays put and only the beat numbers change. Scrolling the ladder
    // yourself pauses this for a couple of seconds.
    bool followPlayhead = false;
    // Each throw's spin (clubs and rings: throwSpins() in juggle_sim.h), for the tooltip when
    // the mouse rests on a throw. Null or empty: none.
    const std::vector<ThrowSpin>* spins = nullptr;
};

constexpr int kNoSelectionChange = -2;

// What the user did this frame. The caller applies it.
struct LadderEditResult {
    bool startedChain = false;  // the user picked up a throw this frame (editing a pattern)
    bool startedDrawing = false;  // the user started drawing in a sketch this frame
    bool committed = false;     // the pattern changed: an edit chain closed, a throw was drawn
                                // or deleted, beats were added or deleted
    JugglingLoop loop;          // the new loop (every juggler; may be a sketch)
    // While drawing with the rubber band over a spot it can land in: what the pattern would be
    // if the user clicked there (so the jugglers can show it before the click).
    bool hasPreview = false;
    JugglingLoop preview;
    // A strip header was clicked: the juggler to select, or -1 to deselect.
    int selectJuggler = kNoSelectionChange;
    // From a juggler's menu: make linkJuggler a copy of linkTo, starting from linkTo's
    // throws[linkStart] ("@2[3]"); or
    // unlink unlinkJuggler (write their throws out). -1: nothing.
    int linkJuggler = -1, linkTo = -1, linkStart = 0;
    int unlinkJuggler = -1;
    // From a juggler's menu: Walk Like... (open the walk link window for this juggler). -1: no.
    int walkLikeJuggler = -1;
    // A keyframe's menu: Delete, or turn the long way round (toggled). -1: nothing.
    int deleteKeyframeJuggler = -1, deleteKeyframeBeat = 0;
    int longTurnKeyframeJuggler = -1, longTurnKeyframeBeat = 0;
    // A keyframe marker dragged to another beat: moveKeyframeJuggler's keyframe on beat
    // moveKeyframeFrom goes to moveKeyframeTo (both in the cycle). -1: nothing.
    int moveKeyframeJuggler = -1, moveKeyframeFrom = 0, moveKeyframeTo = 0;
    // A keyframe Ctrl+dragged to make a loiter: a copy of loiterJuggler's keyframe on beat
    // loiterFrom on beat loiterTo (both in the cycle); loiterBefore: it's the earlier of the two
    // (the juggler arrives at the copy). -1: nothing.
    int loiterJuggler = -1, loiterFrom = 0, loiterTo = 0;
    bool loiterBefore = false;
    // Clicking or dragging in the beat-number column: move the playhead to scrubBeat (a click:
    // the nearest whole beat; a drag: fractional; beat 0 is "beat 1"). scrubEnded: the drag just finished.
    bool scrubbing = false, scrubEnded = false;
    // Home (or Ctrl+0) over the ladder: back to beat 1, the playhead and the view together.
    bool goToStart = false;
    double scrubBeat = 0.0;
};

// Abandons any open edit chain (e.g. when the pattern is changed some other way).
void cancelLadderEdit(LadderEditState& edit);

// Back to the default pan and zoom.
void resetLadderView(LadderEditState& edit);
bool ladderViewIsDefault(const LadderEditState& edit);

// A throw as the ladder labels it: "3", "b", "4p" (for 3+ jugglers, "4p2": to juggler 2).
// thrower is the juggler throwing it.
std::string throwLabel(const LoopThrow& t, int thrower, int jugglers, bool relative = false);

// Fills the remaining content region of the current ImGui window, and handles editing. `loop`
// is the pattern (empty if there's nothing valid to show; it may be a sketch). playheadBeat is
// the playback position (fractional beats, same numbering as the ladder); it's drawn as a line
// at that beat and at the same point in every other repeat on screen (not for a sketch).
LadderEditResult drawLadderDiagram(const JugglingLoop& loop, ColorVisionMode colorVision, LadderEditState& edit,
                                   double playheadBeat, const LadderViewOptions& options);
