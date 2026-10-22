// preferences_window.h - Edit > Preferences: for now, the 3D mouse's response curves (see
// response_curve.h). It sits over the ladder pane while it's open, so the stage stays in view:
// drag a curve's points, try the cap (each axis's reading shows on its curve, live), and drag
// again. Readings are a percentage of each axis's full push (how hard the user pushes it at most),
// which Calibrate measures.
#pragma once

#include "platform.h"
#include "settings.h"

#include "imgui.h"

// The window's own state: a point being dragged, the curves' Y scales (held still meanwhile),
// and a calibration under way.
struct PreferencesWindowState {
    int dragCurve = -1, dragPoint = -1;
    float slideTop = 0.0f, turnTop = 0.0f;  // the Y axes' tops (0: not set yet)
    // Calibrating: the hardest push seen on each axis so far (raw counts; the order of
    // AppSettings::spaceFullPush).
    bool calibrating = false;
    float calibrationMax[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

// Draws the window at `pos`, `size` while *open (its close box clears it). Edits `settings` in
// place; returns true when an edit is finished (a point let go, or a curve reset), so the caller
// can save them.
bool drawPreferencesWindow(bool* open, PreferencesWindowState& state, AppSettings& settings,
                           const SpaceMouseState& spaceMouse, ImVec2 pos, ImVec2 size);
