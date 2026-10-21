// grid_pane.h - the siteswap grid pane along the bottom of the window: the throws (and their
// next column), the choreography's rows, the tooltip for the throw under the mouse, and the status
// line under them. It draws, and says what the user did there; the caller makes the changes (see
// applyGridRequest in app.cpp), so the pattern only changes in one place.
#pragma once

#include "app_state.h"
#include "siteswap_grid.h"

#include "imgui.h"

// Draws the pane in a window at `pos`, `size` (with `flags` as well as its own). `written`: the
// throws as written (empty while they have an error); `shown`: the loop the jugglers juggle (see
// shownLoop in app.cpp). Sets pane.siteswapHoverThrow to the throw under the mouse (for the
// ladder), or -1.
SiteswapGridResult drawGridPane(GridPane& pane, const DocumentState& doc, const StageState& stage,
                                const Playback& playback, const AppSettings& settings, const JugglingLoop& written,
                                const JugglingLoop& shown, ImVec2 pos, ImVec2 size, ImGuiWindowFlags flags);
