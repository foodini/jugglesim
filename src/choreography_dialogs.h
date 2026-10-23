// choreography_dialogs.h - the choreography's dialogs: Shorten the Choreography?, the Walk
// Like... window (with its Link This Juggler? question), the Walking Loop builder, Rename Spike
// Mark, and the loiter popup. Each keeps its own state (below) and says what was chosen; the
// caller makes the change (in app.cpp), so the pattern only changes in one place. Each opens
// itself when its state says it's requested (so it's opened where it's drawn).
#pragma once

#include "choreography.h"
#include "color_vision.h"
#include "juggle_sim.h"
#include "loop_ops.h"

#include "imgui.h"

#include <string>

// Shortening the choreography to a length that leaves keyframes past its end: asks whether to
// delete them.
struct ShortenDialog {
    bool requested = false;  // open the dialog this frame
    int length = 0;          // the length asked for
};

// The loiter popup: after a juggler is put somewhere (dropped, or a mark clicked), how many
// beats they stay there (a second keyframe that many beats later, on the same spot).
struct LoiterPopup {
    bool openRequested = false;
    bool open = false;       // (as of the last frame drawn)
    int juggler = -1;
    Keyframe arrival;        // the keyframe just made
    long onBeat = 0;         // the beat it was made on (as counted, not just in the cycle)
    ImVec2 at;               // where (screen)
    char text[8] = "";
    std::string error;       // a length that doesn't fit, flashed
    double errorUntil = 0.0;
};

// Renaming a spike mark (its menu's Rename...): two characters, 0-9 and a-z, unique.
struct RenameMark {
    int mark = -1;
    bool requested = false;  // open the dialog this frame
    char text[3] = {0, 0, 0};
    std::string error;
};

// The loop column's builder (right-click a walking row's loop cell): whose part, and the turn.
struct LoopBuilder {
    int juggler = -1;
    bool requested = false;
    Continuation k;
};

// The Walk Like... window: one juggler's walk link (see WalkLink), edited live.
struct WalkLikeWindow {
    bool open = false;
    int juggler = -1;
    int confirmLeader = -1;  // asking whether to link to this leader (deleting their keyframes)
    bool confirmRequested = false;
    // The choice under the mouse (a leader, an offset, a turn): the link it would make, shown
    // on the stage as a ghost of the juggler (see WalkGhost) until the mouse moves off.
    bool previewing = false;
    WalkLink preview;
};

// Returns true when Shorten is chosen (delete the keyframes past d.length, and shorten it).
bool drawShortenDialog(ShortenDialog& d, const Choreography& choreography);

struct WalkLikeRequest {
    int juggler = -1;      // the juggler the window is for
    bool setLink = false;  // link them to walk like this:
    WalkLink link;
    bool unlink = false;   // write their walking out as their own keyframes
};
// The window, while w.open. `loop`: the loop shown.
WalkLikeRequest drawWalkLikeWindow(WalkLikeWindow& w, const Choreography& choreography, const JugglingLoop& loop,
                                   const JuggleParams& params);

// Returns true when Set is chosen (b.juggler's loop column to b.k).
bool drawLoopBuilder(LoopBuilder& b, const JugglingLoop& loop, const JuggleParams& params);

struct RenameMarkRequest {
    bool rename = false;  // rename this mark:
    int mark = -1;
    std::string name;
    bool refused = false;  // a name that won't do (plip)
};
RenameMarkRequest drawRenameMarkDialog(RenameMark& d, const Choreography& choreography);

struct LoiterRequest {
    bool loiter = false;  // a keyframe for this juggler, where they arrived, some beats later:
    int juggler = -1;
    Keyframe stay;
};
// `cycle`: the choreography's length (0: none, and the popup doesn't open).
LoiterRequest drawLoiterPopup(LoiterPopup& p, const Choreography& choreography, int cycle, ColorVisionMode colorVision);
