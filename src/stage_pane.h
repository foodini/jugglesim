// stage_pane.h - the 3D view (the juggler pane): what the mouse does there. Selecting and framing
// jugglers, orbiting, panning and zooming the camera, and in choreography mode dragging jugglers
// (keyframes) and spike marks, and the floor's right-click menu. The view's own state (camera,
// selection, drags) it changes itself; changes to the pattern it asks for (StagePaneRequest), and
// the caller makes them (see applyStageRequest in app.cpp), so the pattern only changes in one
// place. Also some questions about the stage that other parts ask too.
#pragma once

#include "app_state.h"
#include "camera.h"
#include "platform.h"
#include "stage_overlays.h"

#include "imgui.h"

#include <string>
#include <utility>
#include <vector>

// What the 3D view shows this frame (worked out by the caller).
struct StageFrame {
    const JugglerScene& scene;        // the stage at this moment
    const JugglingLoop& loop;         // the loop shown and edited (the ladder's preview while drawing)
    const ChoreographyPaths& paths;   // choreography mode: the paths the jugglers walk
    bool showPaths;
    int pathCycle;                    // the beats the paths take, walking loop and all
    ImVec2 viewMin, viewSize;         // the view, on the screen
};

struct StagePaneRequest {
    // A keyframe to set (a juggler dragged somewhere, or the mark chosen among several)...
    int keyframeJuggler = -1;
    Keyframe keyframe;
    // ...and then the loiter popup to open for it (how long they stay).
    int loiterJuggler = -1;
    Keyframe loiterArrival;
    ImVec2 loiterAt;
    // A juggler let go after a drag: the keyframe on the current beat gets a mark if it's on none,
    // and the loiter popup opens (or, among several marks, the choice of mark). Where it was
    // dropped (screen), and the floor there, if the mouse was over it.
    int droppedJuggler = -1;
    ImVec2 dropAt;
    bool dropOnFloor = false;
    Vec3 dropFloor;
    // Spike marks moved, turned, or turned to face the middle: their new places.
    std::vector<std::pair<int, SpikeMark>> movedMarks;
    int renameMark = -1;        // ...the Rename dialog for this one
    int deleteMark = -1;        // ...this one deleted (if nobody stands on it)
    bool addMark = false;       // ...a new one (selected once it's made)
    SpikeMark newMark;
    int walkLikeJuggler = -1;   // the Walk Like... window, for this juggler
    // Playback: paused, and/or moved to this beat.
    bool pause = false;
    bool goToBeat = false;
    double beat = 0.0;
    // A note under the siteswap grid (why something can't be done), for this long.
    std::string note;
    double noteSeconds = 0.0;
    // (Not a change: the choreography path's point under the mouse, or -1, for drawing it lit.)
    int hoveredPathPoint = -1;
};

// The 3D view's input: an invisible window over the view (the view itself is drawn straight to
// OpenGL), with `flags` as well as its own.
StagePaneRequest drawStagePane(StageView& stageView, const DocumentState& doc, const Playback& playback,
                               const AppSettings& settings, const StageFrame& frame, ImGuiWindowFlags flags);

// Spike marks are kept in meters for the distance they were placed at, and scale with it: the
// scale for `loop` at the distance in use.
float choreographyScale(const DocumentState& doc, const JugglingLoop& loop);
// What of the mark groups the floor shows this frame: all of them (badges and lines) while Ctrl is
// held; else the lines of the group under the mouse in the panel, or of the one selected.
MarkGroupView markGroupView(const StageView& stageView, const DocumentState& doc);
// The spike marks whose rings contain a floor point (world).
std::vector<int> marksAround(const Choreography& c, float x, float z, float scale);
// The stage a free camera orbits within: the jugglers, the spike marks and keyframe spots, with a
// margin, from the floor to a bit above head height.
StageBox stageBoxFor(const JugglerScene& scene, const DocumentState& doc, const JugglingLoop& loop);
// How far a point (the free camera) is from the nearest thing on stage: a juggler, a prop or a
// spike mark. Dollying goes at a speed in proportion, so it's quick from far away and fine up close.
float nearestToCamera(const JugglerScene& scene, const DocumentState& doc, const JugglingLoop& loop, Vec3 p);

// The camera after this frame's input: the top view's framing, the 3D mouse (grabbing the stage:
// see the .cpp), and the automatic framing or free camera (see camera.h). Remembers it in
// stageView (lastCamera, lastViewProj, lastAspect: for picking next frame). loiterOpen: the loiter
// popup is up (the top view doesn't shrink under it).
CameraView updateStageCamera(StageView& stageView, const DocumentState& doc, const StageState& stage,
                             const AppSettings& settings, const StageFrame& frame, const SpaceMouseState& spaceMouse,
                             bool loiterOpen);

// What's drawn over the 3D view: the floor overlays (their lines into stageView.overlayTriangles,
// for the 3D pass; their text into ImGui's background), the orbit reticle, the red tint while the
// pattern has a problem, and the juggler numbers and throw values.
void drawStageOverlays(StageView& stageView, const DocumentState& doc, const StageState& stage,
                       const AppSettings& settings, const StageFrame& frame, const CameraView& cameraView,
                       int hoveredPathPoint, const std::vector<int>& styleOfBall);
