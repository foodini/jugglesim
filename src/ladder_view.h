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
    enum class Context { None, Beat, Throw, Juggler };
    Context context = Context::None;
    int contextBeat = 0;            // Beat: the beat line clicked
    int contextJuggler = 0;         // Juggler: whose number (strip header) was right-clicked
    Slot contextSlot;               // Throw: where the throw clicked is thrown from
    std::vector<int> pathHighlight; // slots to highlight (hovering "Delete path"): juggler * period + beat,
    int pathHighlightPeriod = 1;    // with this period (the props' cycle, which may be longer than the loop's)

    // View: pan and zoom.
    float firstBeat = 0.0f;        // beat drawn at the top of the ladder (may be negative)
    float zoom = 1.0f;
    bool beatOneVisible = true;    // as of the last frame drawn
    bool scrubbing = false;        // dragging the playhead in the beat-number column

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

// How to draw the ladder (things owned by the rest of the app).
struct LadderViewOptions {
    bool showValues = false;    // label every throw with its value ("3", "4p")
    bool colorByOrbit = false;  // one color per orbit instead of per prop
    int selectedJuggler = -1;   // shown highlighted in the strip headers (-1: none)
    // A throw picked out from elsewhere (hovering it in the siteswap text box), at every repeat:
    // juggler * period + beat in the loop (-1: none).
    int highlightThrow = -1;
    bool relativeTargets = false;  // label passes "3p+1" (the pattern's text does) rather than "3p2"
    // How the pattern was typed: which jugglers are links ("@2+3"), for their strip headers, the
    // Same as.../Unlink menu and dimming. May be null.
    const PatternForm* form = nullptr;
    bool dimLinked = false;  // draw linked jugglers' throws muted, so the parts written out stand out
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
    // From a juggler's menu: make linkJuggler the same as linkTo, linkOffset beats later; or
    // unlink unlinkJuggler (write their throws out). -1: nothing.
    int linkJuggler = -1, linkTo = -1, linkOffset = 0;
    int unlinkJuggler = -1;
    // Clicking or dragging in the beat-number column: move the playhead to scrubBeat (fractional;
    // beat 0 is "beat 1"). scrubEnded: the drag just finished.
    bool scrubbing = false, scrubEnded = false;
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
