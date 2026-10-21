// panels.h - the floating panels over the 3D view: Tweakables (tempo, dwell, distance) and
// Choreography.
#pragma once

#include "choreography.h"
#include "juggle_sim.h"
#include "settings.h"
#include "siteswap.h"
#include "imgui.h"

#include <string>
#include <vector>

// ---- Tweakables panel ---------------------------------------------------------------------
//
// ("Tweakables" is a working name.) A small panel in the top-right corner of the juggler pane:
// tempo, dwell and, for passing patterns, the distance between the jugglers. Each has a slider with
// -/+ buttons for fine steps (Shift: bigger steps; hold a button to repeat), the mouse wheel
// over the slider for one step, and Ctrl+click or double-click on the slider to type a value.
// The panel collapses to a one-line readout.

// A slider's clicks (see fineSlider in panels.cpp): what the value was before them, so a
// double-click that opens the text field can put back what the first click did.
struct FineSliderState {
    double beforeClick = 0.0;          // the value before the latest click on the slider
    double beforePreviousClick = 0.0;  // and before the one before that
};

struct TweakablesPanelState {
    FineSliderState tempo, dwell, distance;
};

void resetTweakables(JuggleParams& params);

// Draws the panel with its top-right corner at `topRight`. `loop` is the pattern being shown
// (the distance row appears for passing patterns). Returns true if the user collapsed or
// expanded it (so the caller can remember that).
bool drawTweakablesPanel(JuggleParams& params, const JugglingLoop& loop, bool& collapsed, ImVec2 topRight,
                         TweakablesPanelState& state, bool distanceLocked, float* bottom);

// ---- Choreography panel -------------------------------------------------------------------

constexpr int kMaxChoreographyLength = 1024;  // beats (longer routines: chain choreographies, later)

// A turn (see Turn), edited as it's typed: a direction, then numerator / denominator, each with
// - and + (CCW (o) CW ( )   [2] - +  /  [5] - +   (144°)). The fraction isn't reduced. True when
// *turn changes; *hovered (if given) is set to what the button under the mouse would make it.
bool turnEditor(const char* id, Turn* turn, Turn* hovered = nullptr);

struct ChoreographyPanelState {
    FineSliderState spacing, spokes, major;
    // Groups of spike marks: how many marks each has (Choreography::kMarkGroups of them), and what
    // was done with their chips this frame (-1: nothing): rested on, clicked (select it), Shift+
    // clicked (add it), emptied (its menu).
    std::vector<int> groupSizes;
    int hoveredGroup = -1, clickedGroup = -1, addedGroup = -1, emptiedGroup = -1;
};

// In choreography mode, under the Tweakables: the floor grid that spike marks (and juggler
// drops) snap to. Returns true if a setting changed (so the caller can save them).
// *newLength is set (else left alone) when the user asks for a new choreography length, and
// `phases`: the walking loop's times round (for the length in all).
// `period`: the pattern's period (Ctrl+-/+ steps the length a whole period).
bool drawChoreographyPanel(AppSettings& settings, ImVec2 topRight, ChoreographyPanelState& state, int length,
                           int* newLength, int phases, int period);
