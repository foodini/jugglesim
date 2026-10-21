// app_state.h - everything the app keeps from frame to frame, in one place (App), grouped by
// what it's for. runApp() (app.cpp) owns one App.
#pragma once

#include "camera.h"
#include "choreography.h"
#include "juggle_sim.h"
#include "ladder_view.h"
#include "loop_ops.h"
#include "mesh.h"
#include "panels.h"
#include "pattern.h"
#include "pattern_library.h"
#include "pattern_library_ui.h"
#include "prop_figure.h"
#include "renderer.h"
#include "settings.h"
#include "siteswap.h"
#include "siteswap_grid.h"

#include "imgui.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Undo/redo history of the pattern, kept as siteswap text (which also records the loop
// period: 531 and 531531 are different entries). One step per closed edit chain, period
// change, siteswap committed in the text box, or pattern loaded from the library. Steps can
// also carry the settings (props, tempo, dwell, distance) from before and after: loading from
// the library changes both, and changing a setting by itself is a step of its own once the
// user has stopped adjusting it for a moment.
struct HistoryStep {
    std::string before, after;
    bool settingsChanged = false;
    PatternSettings settingsBefore, settingsAfter;
};
struct PatternHistory {
    std::vector<HistoryStep> undo;
    std::vector<HistoryStep> redo;
    std::string current;  // text of the pattern being shown

    // Call when the pattern has changed to `next` by a user action.
    void record(const std::string& next) {
        if (next == current) return;
        HistoryStep step;
        step.before = current;
        step.after = next;
        push(step);
    }
    // Call when a library pattern has been loaded (settings: all of them, before and after).
    void recordWithSettings(const std::string& next, const PatternSettings& before,
                            const PatternSettings& after) {
        if (next == current && before == after) return;
        HistoryStep step;
        step.before = current;
        step.after = next;
        step.settingsChanged = before != after;
        step.settingsBefore = before;
        step.settingsAfter = after;
        push(step);
    }
    void push(const HistoryStep& step) {
        undo.push_back(step);
        if (undo.size() > 1000) undo.erase(undo.begin());
        redo.clear();
        current = step.after;
    }
};

// Playback clock. beat is fractional (beat 0 = the right hand's first throw) and may be
// negative after stepping back; the pattern repeats forever in both directions.
struct Playback {
    bool playing = true;
    double beat = 0.0;
    bool slow = false;          // slow motion (at AppSettings::slowRate)
};
constexpr double kStepBeats = 1.0 / 12.0;  // one arrow-key press; Shift steps a whole beat

// What the line under the siteswap grid says for a while: a problem (why something couldn't be
// done), or a note that isn't one ("Saved ...").
struct SiteswapBox {
    std::string note;            // why the last edit didn't happen
    double noteUntil = 0.0;      // (shown until then)
    std::string info;            // a note that isn't a problem ("Saved ..."), shown in plain text
    double infoUntil = 0.0;      // (shown until then)
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

// Picking one of several spike marks in the same place: after a juggler is put there, their
// arrows light up and a click on one (or its number) says which.
struct MarkChoice {
    bool active = false;
    int juggler = -1;
    Keyframe pending;        // the keyframe made (on one of them for now)
    std::vector<int> marks;  // the marks to choose from
    ImVec2 at;               // where (screen), for the loiter popup after
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

// File > New Pattern...: a blank sketch (every throw "?") for some jugglers and period.
struct NewPatternDialog {
    bool openRequested = false;
    int jugglers = 2;
    int period = 6;
    int count = 0;  // a count directive (0: none): blanks only on its beats, 3s between
    int walking = 0;  // the choreography's length, its own period (0: no choreography yet)
};

// Graphics: what the 3D view is drawn with (made once the window and OpenGL are up).
struct Graphics {
    ImFont* titleFont = nullptr;
    Renderer renderer;
    Primitives prims;
    PropMeshes propMeshes;
};

// The pattern being edited: its throws (as typed, and read), the choreography, the settings that
// go with it, and what was last saved (for the save prompts). (To become the Document.)
struct DocumentState {
    // The pattern (throw events) is the source of truth for juggling. The siteswap text box
    // generates it when the user types, and is rewritten from it when the pattern changes some
    // other way (e.g. the toolbar's period control). While the text is invalid, the last valid
    // pattern is kept but not shown.
    char siteswapText[4096] = "531";  // (room for 6 jugglers x 100 beats or so)
    int patternCount = 0;  // its count directive ("4-count"), 0: none
    Siteswap parsed;
    Pattern pattern;
    bool patternValid = false;
    // A sketch (a pattern with throws not decided yet, "?") is shown and edited on the ladder,
    // while the jugglers carry on with the last complete pattern (`pattern`), dimmed.
    bool sketching = false;
    JugglingLoop sketchLoop;
    // The pattern as last loaded, saved or started: File > New Pattern... and quitting offer to
    // save first if what's being juggled has changed since: its siteswap or its choreography
    // (tempo, dwell and distance don't count).
    std::string savedPatternText;
    PatternHistory history;
    JuggleParams juggleParams;
    // Where the jugglers stand and walk: spike marks and keyframes (none: the default formation,
    // standing still). See choreography.h.
    Choreography choreography;
    // The throwing loop (the siteswap grid's next column): whose throws each juggler takes up after
    // the period (-1: their own; see loop_ops.h). Saved with the pattern ("next=2,1").
    std::vector<int> throwNext;
    // (For the save prompts: the choreography as last loaded, saved or started.)
    std::string savedChoreography;
    std::string savedThrowNext;
    // The choreography as ensureSpikeMarks made it (the default formation), while it's untouched;
    // once anything in it changes, the pattern's distance is fixed (see choreographyLocked).
    std::string untouchedChoreography;
    // The choreography's floor grid last put in the settings: one loaded (a pattern, undo) that
    // differs is put in place.
    std::string choreographyGridShown;
    PatternSettings recordedSettings;
    PatternSettings lastSeenSettings;
    double settingsChangedAt = 0.0;
    // The one of My Patterns that's open (its name as shown), so File > Save can save over it;
    // empty if what's showing isn't one of them (new, typed, or from JuggleSim's patterns).
    std::string openPatternName;
};

// What the stage plays: the loops worked out from the pattern, as they last worked while the
// pattern has an error; and the camera framing worked out for them.
struct StageState {
    // What the stage shows: the choreography, except while its walking loop doesn't work (a part
    // taken twice, or by nobody), when it stays as it last was that did (and the juggler pane is
    // tinted red), so where everyone is can still be seen while it's put right.
    Choreography stageChoreography;
    bool walkLoopBroken = false;
    std::string walkLoopProblem;  // ...and why (shown under the grid while it lasts)
    // ...the pattern played out with it this frame (and the walking loop's paths), and what the
    // stage plays: the same, except while it doesn't work (a part taken twice, props landing
    // together), when the stage keeps the last that did (tinted red), like the walking loop.
    PlayedLoop played;
    WalkPaths playPaths;
    JugglingLoop stagePlayLoop;
    // The throws as they last worked (written out, not played; a sketch counts): while they have
    // an error, the walking (its grid rows, its dialogs) keeps to this, so it doesn't vanish as
    // you type. (Only while the throws have the same jugglers: one left from an earlier
    // pattern isn't this one.)
    JugglingLoop lastGoodLoop;
    bool throwLoopBroken = false;
    bool throwsBroken = false;  // the throws have an error: the stage plays the last pattern that worked
    std::string throwLoopProblem;
    // Pattern extents depend only on the loop, timing and framing, so they're cached. They take
    // a while to work out for a long passing pattern (a few tenths of a second), so while the
    // pattern keeps changing (typing, rotating, dragging a tweakable) they wait until it has
    // settled for a moment; the camera glides to the new framing anyway.
    JugglingLoop extentsLoop;
    JuggleParams extentsParams{-1.0, -1.0};
    std::string extentsChoreography;  // (text form)
    int extentsJuggler = -2;
    SceneExtents extents;
    bool haveExtents = false;
    JugglingLoop extentsSeenLoop;  // what the extents are wanted for, as of the last frame...
    JuggleParams extentsSeenParams{-1.0, -1.0};
    std::string extentsSeenChoreography;
    double extentsSeenSince = 0.0;  // ...and since when
};

// The 3D view (the juggler pane): its camera, selection, and what the mouse is doing there.
struct StageView {
    bool choreographyMode = false;  // View > Choreography (M): spike marks shown and editable
    int markDrag = -1;  // the spike mark being dragged, or -1
    int jugglerDrag = -1;  // the juggler being dragged to a new spot (a keyframe), or -1
    int viewPress = -1;  // the mouse button pressed in the juggler view (0 left, 1 right, 2 middle)
    float jugglerDragYaw = 0.0f;  // ...facing as they were when the drag started
    bool markTurning = false;  // ...by its arrow (turning it) rather than its middle
    bool markForKeyframe = false;  // ...and if it turns out to be a click, it's for a keyframe there
    int markMenuMark = -1;  // the floor's right-click menu: on this mark, or -1...
    int floorMenuJuggler = -1;  // ...and on this juggler, or -1
    Vec3 markMenuPoint;  // ...at this floor point
    CameraView lastCamera{};  // the 3D view as last drawn (for finding the floor under the mouse)
    float lastAspect = 1.0f;
    MarkChoice markChoice;
    // Spike marks selected: a click on one selects it (and no juggler), Shift+click adds one or
    // takes it away; dragging any of them moves them all, keeping their shape. Esc, or a click on
    // empty floor or a juggler, clears it. (Keyframes come from dragging a juggler onto a mark.)
    std::vector<int> selectedMarks;
    std::vector<std::pair<float, float>> markDragStart;  // ...where each was when the drag began (unscaled)
    bool markPressWasSelected = false;  // ...whether the one pressed already was
    std::vector<int> selectedMarksSaid;  // (the selection the status line last described)
    // Groups of marks (Choreography::groups): the one just selected by its key or chip (while the
    // selection is still exactly it: its lines show), and the chip the mouse rests on (-1: none).
    int selectedGroup = -1;
    int groupChipHovered = -1;
    LoopBuilder loopBuilder;
    bool pressTakenByChoice = false;  // the current press in the juggler view picked a mark
    CameraControl camera;  // the juggler view's camera (a pattern can bring a free camera along)
    double spaceMouseQuietUntil = 0.0;  // ignore the mouse wheel until then (the 3D mouse is moving)
    double spaceMouseWheelBlocked = -10.0;  // when wheel input was last ignored for that
    int pathPress = -1;  // a press on a choreography path's point (its index), to go there on release
    std::vector<GlowVertex> overlayTriangles;  // this frame's floor overlays (see OverlayBuilder)
    std::vector<float> jugglerLabelCenters;  // the juggler labels' screen x last frame (see drawJugglerLabels)
    // Orbiting a free camera: about this center, fixed for the length of each orbit (the
    // reticle shows it; the rest of the time it's worked out afresh, and shown faintly).
    Vec3 orbitCenter;
    bool cameraOrbiting = false;  // the mouse is orbiting the camera
    bool cameraPanning = false;  // ...or panning it
    bool viewPan = false;  // the press in the juggler view pans (middle, or Shift+right)
    bool viewOrbitLeft = false;  // ...a left press that orbits (Alt/Option held: for a trackpad)
    float panDepth = 3.0f;  // panning: how far away the floor under the mouse was
    double spaceOrbitUntil = 0.0;  // the 3D mouse is orbiting (until this time)
    // Selection in the 3D view (passing patterns): the selected juggler, or -1. With
    // frameSelected the camera frames just that juggler.
    int selectedJuggler = -1;
    bool frameSelected = false;
    Mat4 lastViewProj = Mat4::identity();  // the 3D view's camera as last drawn (for picking)
};

// The ladder pane.
struct LadderPane {
    // While drawing with the rubber band over a spot it can land in, the jugglers show what
    // clicking there would make.
    bool ladderPreview = false;
    JugglingLoop ladderPreviewLoop;
    LadderEditState ladderEdit;  // drag-editing and pan/zoom state for the ladder
    // Each throw's spin, for the ladder's tooltips (recomputed when the pattern or timing changes).
    std::vector<ThrowSpin> spinCache;
    JugglingLoop spinCacheLoop;
    JuggleParams spinCacheParams{-1.0, -1.0};
};

// The siteswap grid along the bottom.
struct GridPane {
    SiteswapBox siteswapBox;
    SiteswapGridState siteswapGrid;  // the siteswap grid along the bottom (cursor, typing, dialogs)
    int siteswapHoverThrow = -1;  // the throw under the mouse in the text box (for the ladder)
};

// The pattern library: built-in patterns, the user's own (saved, renamed and deleted from the File
// menu), and the recently loaded ones; and its menus and dialogs.
struct LibraryState {
    std::vector<LibraryPattern> builtInPatterns;
    std::vector<LibraryPattern> myPatterns;
    std::vector<RecentPattern> recentPatterns;
    PatternMenuState juggleSimMenuState;
    PatternMenuState myMenuState;
    SavePatternDialog saveDialog;
    ManagePatternsWindow manageWindow;
    std::map<std::string, std::string> patternNames;
};

// Windows, dialogs, prompts and panels.
struct UiState {
    std::string windowTitle;  // as last set (it's set only when it changes)
    bool savePromptRequested = false;
    bool promptForQuit = false;  // that prompt is for quitting (not for File > New Pattern...)
    bool newAfterSave = false;  // Save... was chosen in that prompt: start the new pattern once saved
    bool quitAfterSave = false;  // ...or quit once saved
    LoiterPopup loiter;
    double lastLoiterErrorUntil = 0.0;
    RenameMark renameMark;
    WalkLikeWindow walkLike;
    std::string fileError;  // shown in a message box when set
    // File > Save (Ctrl+S): over the open pattern, or (if none) asking for a name as Save As does.
    bool saveRequested = false;
    bool saveAsRequested = false;
    TweakablesPanelState tweakablesPanel;
    int pendingChoreographyLength = 0;  // asking whether to shorten the choreography to this
    std::set<std::string> dismissedWarnings;  // (keys of) warnings the user has dismissed
    ChoreographyPanelState choreographyPanel;
    bool showImGuiDemo = false;
    bool showShortcuts = false;  // the keyboard shortcuts sheet (? or Help > Keyboard Shortcuts)
    bool showSpaceMouseDiagnostics = false;  // Help > 3D Mouse Diagnostics
    bool showColorPreview = false;
    bool done = false;
    bool closePending = false;  // the window's close box was clicked (handled once it's showing)
    NewPatternDialog newPatternDialog;
};

// Sounds wanted this frame (the plip, the metronome's tick), and what they were last made for.
struct SoundState {
    // The plip (see sound.h): wanted this frame; and the notes already plipped for.
    bool plipWanted = false;
    // The metronome (A): the beat it last ticked for, and the tick wanted this frame (0: none, 1:
    // a beat, 2: a count's first beat).
    long metronomeBeat = 0;
    int tickWanted = 0;
    double lastNoteUntil = 0.0;
};

// Everything, grouped. runApp() works with these through references named after them (doc,
// stage, stageView, ...), so the code reads the same as it will in the panes' own functions.
struct App {
    AppSettings settings;
    Graphics gfx;
    DocumentState doc;
    StageState stage;
    Playback playback;
    StageView stageView;
    LadderPane ladderPane;
    GridPane gridPane;
    LibraryState library;
    UiState ui;
    SoundState sound;
};
