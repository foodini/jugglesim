// ladder_pane.h - the ladder pane (top left): its toolbar and the ladder diagram, with the
// choreography's keyframes on it. It draws, and says what the user did there; the caller makes
// the changes (see applyLadderRequest in app.cpp), so the pattern only changes in one place.
#pragma once

#include "app_state.h"
#include "ladder_view.h"

#include "imgui.h"

struct LadderPaneRequest {
    // The toolbar's buttons. (Collapse all and reset view only change the ladder's own view: those
    // are done already.)
    LadderToolbarRequest toolbar;
    // What was done in the diagram: an edit committed, a keyframe moved, a juggler selected...
    LadderEditResult edited;
};

// Draws the pane in a window at `pos`, `size` (with `flags` as well as its own). `written`: the
// throws as written (empty while they have an error); `shown`: the loop the jugglers juggle (see
// shownLoop in app.cpp); `hoverThrow`: the throw the siteswap grid has under the mouse (picked
// out on the ladder), or -1. Sets pane.ladderPreview (and its loop) while drawing shows what a
// click would make.
LadderPaneRequest drawLadderPane(LadderPane& pane, const DocumentState& doc, const StageState& stage,
                                 const StageView& stageView, int hoverThrow, const Playback& playback,
                                 const AppSettings& settings, const JugglingLoop& written, const JugglingLoop& shown,
                                 ImVec2 pos, ImVec2 size, ImGuiWindowFlags flags);
