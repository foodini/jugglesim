// ladder_view.h - ladder diagram of a pattern, drawn with ImGui's draw list.
//
// Time runs left to right. The top rail is the right hand, the bottom rail the left hand,
// with beat 1 thrown by the right hand. Each throw is a curve from its throw beat to its
// landing beat, styled (color + marker shape + dash pattern) by which ball it carries.
//
// Read-only for now. The canvas is an ImGui InvisibleButton so later editing can hit-test it.
//
// Above the diagram is a toolbar row for ladder modes and tools. It doesn't change anything
// itself: it returns what the user asked for, and the caller applies it.
#pragma once

#include "color_vision.h"
#include "ladder_edit.h"
#include "pattern.h"

#include <climits>
#include <vector>

// Zoom limits for the ladder (1 = default beat spacing).
constexpr float kMinLadderZoom = 0.3f;
constexpr float kMaxLadderZoom = 4.0f;

// Editing and view state that persists between frames. Owned by the caller.
struct LadderEditState {
    EditChain chain;               // the open edit chain, if any
    int heldBall = -1;             // ball id of the held throw (-1 = an empty beat)
    int hoverBeat = INT_MIN;       // idle: departure beat of the highlighted throw
    HeldEnd hoverEnd = HeldEnd::Arrival;
    int hoverTarget = INT_MIN;     // holding: the highlighted drop beat

    // View: pan and zoom.
    float firstBeat = 0.0f;        // beat drawn at the left edge of the ladder (may be negative)
    float zoom = 1.0f;
    bool beatOneVisible = true;    // as of the last frame drawn
};

struct LadderToolbarRequest {
    int newPeriodBeats = 0;  // 0 = no change
    bool resetView = false;
};

// Draws the toolbar row (mode toggle, period control, reset view). Call before
// drawLadderDiagram.
LadderToolbarRequest drawLadderToolbar(const Pattern& pattern, bool patternValid,
                                       const LadderEditState& edit);

// What the user did to the pattern this frame. The caller applies it.
struct LadderEditResult {
    bool startedChain = false;  // the user picked up a throw this frame
    bool committed = false;  // an edit chain closed
    std::vector<int> loop;   // the new loop values (same period as before)
};

// Abandons any open edit chain (e.g. when the pattern is changed some other way).
void cancelLadderEdit(LadderEditState& edit);

// Back to the default pan and zoom.
void resetLadderView(LadderEditState& edit);
bool ladderViewIsDefault(const LadderEditState& edit);

// Fills the remaining content region of the current ImGui window, and handles drag editing.
// playheadBeat is the playback position (fractional beats, same numbering as the ladder); it's
// drawn as a line at that beat and at the same point in every other repeat on screen.
LadderEditResult drawLadderDiagram(const Pattern& pattern, bool patternValid,
                                   ColorVisionMode colorVision, LadderEditState& edit,
                                   double playheadBeat);
