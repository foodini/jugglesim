// stage_pane.cpp - see stage_pane.h.

#include "stage_pane.h"

#include "camera.h"
#include "stage_overlays.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

float choreographyScale(const DocumentState& doc, const JugglingLoop& loop) {
    if (doc.choreography.reference <= 0.0 || loop.empty()) return 1.0f;
    return static_cast<float>(passingDistance(loop, doc.juggleParams) / doc.choreography.reference);
}

MarkGroupView markGroupView(const StageView& stageView, const DocumentState& doc) {
    MarkGroupView v;
    if (!stageView.choreographyMode || !doc.choreography.active()) return v;
    if (ImGui::GetIO().KeyCtrl) {
        v.slots = doc.choreography.groupsUsed();
        for (int g = 0; g < v.slots; ++g)
            if (!doc.choreography.group(g).empty()) v.lines.push_back(g);
    } else if (stageView.groupChipHovered >= 0) {
        v.lines.push_back(stageView.groupChipHovered);
    } else if (stageView.selectedGroup >= 0 && !doc.choreography.group(stageView.selectedGroup).empty()) {
        v.lines.push_back(stageView.selectedGroup);
    }
    return v;
}

std::vector<int> marksAround(const Choreography& c, float x, float z, float scale) {
    std::vector<int> found;
    for (int m = 0; m < static_cast<int>(c.marks.size()); ++m) {
        const SpikeMark& sm = c.marks[static_cast<size_t>(m)];
        if (std::hypot(x - sm.x * scale, z - sm.z * scale) <= kMarkRadius * scale) found.push_back(m);
    }
    return found;
}

StageBox stageBoxFor(const JugglerScene& scene, const DocumentState& doc, const JugglingLoop& loop) {
    StageBox box;
    bool any = false;
    auto include = [&](float x, float z) {
        if (!any) {
            box.min = Vec3(x, 0.0f, z);
            box.max = Vec3(x, 2.5f, z);
            any = true;
        }
        box.min.x = std::min(box.min.x, x);
        box.min.z = std::min(box.min.z, z);
        box.max.x = std::max(box.max.x, x);
        box.max.z = std::max(box.max.z, z);
    };
    for (const JugglerState& js : scene.jugglers) include(js.position.x, js.position.z);
    const float scale = choreographyScale(doc, loop);
    for (const SpikeMark& m : doc.choreography.marks) include(m.x * scale, m.z * scale);
    const int cycle = loop.empty() ? 0 : choreographyCycle(loop, doc.juggleParams);
    const int walkers =
        std::max(static_cast<int>(doc.choreography.keys.size()), static_cast<int>(doc.choreography.links.size()));
    for (int j = 0; j < walkers; ++j)
        for (const Keyframe& k : effectiveKeyframes(doc.choreography, j, cycle)) {
            float x = 0.0f, z = 0.0f, yaw = 0.0f;
            keyframeSpot(doc.choreography, k, &x, &z, &yaw);
            include(x * scale, z * scale);
        }
    if (!any) return StageBox();
    box.min = Vec3(box.min.x - 1.5f, 0.0f, box.min.z - 1.5f);
    box.max = Vec3(box.max.x + 1.5f, 2.5f, box.max.z + 1.5f);
    return box;
}

float nearestToCamera(const JugglerScene& scene, const DocumentState& doc, const JugglingLoop& loop, Vec3 p) {
    float nearest = 1e9f;
    for (const JugglerState& js : scene.jugglers) {
        nearest = std::min(nearest, length(p - js.position));
        nearest = std::min(nearest, length(p - Vec3(js.position.x, 1.25f, js.position.z)));
    }
    for (const BallState& b : scene.balls) nearest = std::min(nearest, length(p - b.center));
    const float scale = choreographyScale(doc, loop);
    for (const SpikeMark& m : doc.choreography.marks)
        nearest = std::min(nearest, length(p - Vec3(m.x * scale, 0.0f, m.z * scale)));
    if (nearest >= 1e9f) nearest = 3.0f;
    return std::max(nearest, 0.3f);
}

namespace {

// With rotated repeats, only the beats written out are edited: the rest are them turned.
// Whether `beat` falls in a rotated repeat (and if so, *note says so).
bool inRotatedRepeat(const DocumentState& doc, const JugglingLoop& loop, long beat, std::string* note) {
    if (loop.empty() || doc.choreography.repeats() == 1) return false;
    const int written = std::max(1, choreographyCycle(loop, doc.juggleParams));
    const int whole = choreographyFullCycle(doc.choreography, written);
    const int b = static_cast<int>(((beat % whole) + whole) % whole);
    if (b < written) return false;
    *note = "Beat " + std::to_string(b + 1) + " is in the walking loop's copies (beats " + std::to_string(written + 1) +
            "-" + std::to_string(whole) + " come from beats 1-" + std::to_string(written) +
            " by the C rows' next column): place jugglers on beats 1-" + std::to_string(written) + " instead.";
    return true;
}

}  // namespace

StagePaneRequest drawStagePane(StageView& stageView, const DocumentState& doc, const Playback& playback,
                               const AppSettings& settings, const StageFrame& frame, ImGuiWindowFlags flags) {
    // Mouse input for the 3D view: an invisible window over the juggler pane (the 3D view
    // itself is drawn straight to OpenGL, not by ImGui). Right-drag to orbit, wheel to zoom; with
    // passing patterns, click a juggler to select them (click empty space to deselect) and
    // double-click to frame them (double-click empty space to frame everyone).
    StagePaneRequest r;
    const ImGuiIO& io = ImGui::GetIO();
    const JugglerScene& scene = frame.scene;
    const JugglingLoop& loop = frame.loop;
    const ChoreographyPaths& paths = frame.paths;
    const ImVec2 viewMin = frame.viewMin;
    const ImVec2 viewSize = frame.viewSize;
    ImGui::SetNextWindowPos(viewMin);
    ImGui::SetNextWindowSize(viewSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Juggler view input", nullptr,
                     flags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 1.0f && avail.y > 1.0f) {
            // The mouse: the left button acts on things (selects a juggler; in choreography,
            // drags jugglers and spike marks); the right button drags to orbit the camera
            // (Shift: pan; the middle button pans too), and a right-click without dragging opens
            // the floor's menu. Alt/Option+left-drag orbits as well, for a trackpad.
            ImGui::InvisibleButton("##juggler_view", avail,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle);
            if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) {
                stageView.viewPress = -1;
                stageView.pathPress = -1;
            }
            if (ImGui::IsItemActivated())
                stageView.viewPress = ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                                          ? 0
                                          : (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? 1 : 2);
            const bool leftPress = stageView.viewPress == 0;
            const float clickSlop = io.MouseDragThreshold;
            const bool dragged =
                io.MouseDragMaxDistanceSqr[std::max(0, stageView.viewPress)] >= clickSlop * clickSlop;
            // Choreography: the spike marks can be dragged (moved, or turned by the arrow) and
            // clicked (a keyframe there for the selected juggler, on the current beat).
            const bool editingFloor = stageView.choreographyMode && doc.choreography.active();
            const float markScale = choreographyScale(doc, loop);
            const bool canSelect = scene.jugglers.size() > 1;
            if (ImGui::IsItemActivated()) {
                stageView.viewPan = stageView.viewPress == 2 || (stageView.viewPress == 1 && io.KeyShift);
                stageView.viewOrbitLeft = stageView.viewPress == 0 && io.KeyAlt;
            }
            // A path point under the mouse (a click on it goes to its beat, with its juggler
            // selected).
            if (frame.showPaths && ImGui::IsItemHovered() && stageView.markDrag < 0 && stageView.jugglerDrag < 0)
                r.hoveredPathPoint = pickPathPoint(paths, stageView.lastViewProj, viewMin, viewSize, io.MousePos,
                                                 BodyOccluder(scene, stageView.lastCamera.position));
            if (ImGui::IsItemActivated() && leftPress && !stageView.pressTakenByChoice)
                stageView.pathPress = r.hoveredPathPoint;
            const int underMouse = canSelect && ImGui::IsItemHovered()
                                       ? pickJuggler(scene, stageView.lastViewProj, viewMin, viewSize, io.MousePos)
                                       : -1;
            // Any part of a mark can be dragged: anywhere inside its ring moves it, and its
            // arrow turns it. A click on it puts the selected juggler there. Someone standing on
            // it is picked instead only where the mouse is squarely on their body (no slack),
            // so the floor showing inside the ring is the mark's. The arrow beats everything.
            bool onArrow = false;
            Vec3 floorHere(0.0f, 0.0f, 0.0f);
            const bool haveFloor = editingFloor && floorUnderMouse(stageView.lastCamera, stageView.lastAspect,
                                                                   viewMin, viewSize, io.MousePos, &floorHere);
            // (A mark's name comes first: it picks that mark, even among several on one spot.)
            const int labelUnderMouse =
                editingFloor && ImGui::IsItemHovered() && stageView.markDrag < 0
                    ? markLabelUnder(doc.choreography, markScale, stageView.lastViewProj, viewMin, viewSize,
                                     io.MousePos, markGroupView(stageView, doc).badgeH())
                    : -1;
            const int markUnderMouse =
                labelUnderMouse >= 0 ? labelUnderMouse
                : editingFloor && ImGui::IsItemHovered() && stageView.markDrag < 0
                    ? pickSpikeMark(doc.choreography, markScale, stageView.lastViewProj, viewMin, viewSize,
                                    io.MousePos, haveFloor, floorHere, &onArrow)
                    : -1;
            const bool onBody =
                labelUnderMouse < 0 && underMouse >= 0 && markUnderMouse >= 0 && !onArrow &&
                pickJuggler(scene, stageView.lastViewProj, viewMin, viewSize, io.MousePos, 0.0f) >= 0;
            const bool markRing = markUnderMouse >= 0 && (onArrow || !onBody);
            // A mark (or its arrow) comes before the path points drawn on it (a juggler standing
            // on a mark has a path point in its middle).
            if (markRing) {
                r.hoveredPathPoint = -1;
                if (ImGui::IsItemActivated()) stageView.pathPress = -1;
            }
            // Choosing among marks in the same place: a click on an arrow's tip or its number picks
            // that mark; a click anywhere else keeps the one they're on for now.
            if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) stageView.pressTakenByChoice = false;
            if (stageView.markChoice.active && ImGui::IsItemActivated() && leftPress) {
                stageView.pressTakenByChoice = true;
                std::vector<bool> shown;
                const std::vector<ImVec2> bubbles =
                    markBubbleCenters(doc.choreography, markScale, stageView.lastViewProj, viewMin, viewSize,
                                      &shown, markGroupView(stageView, doc).badgeH());
                int best = -1;
                float bestD = ImGui::GetFontSize() * 1.4f;
                for (const int m : stageView.markChoice.marks) {
                    if (m < 0 || m >= static_cast<int>(doc.choreography.marks.size())) continue;
                    Vec3 middle, tip;
                    markGeometry(doc.choreography.marks[static_cast<size_t>(m)], markScale, &middle, &tip);
                    ImVec2 t;
                    if (projectToScreen(stageView.lastViewProj, tip, viewMin, viewSize, &t)) {
                        const float d = std::hypot(t.x - io.MousePos.x, t.y - io.MousePos.y);
                        if (d < bestD) {
                            bestD = d;
                            best = m;
                        }
                    }
                    if (shown[static_cast<size_t>(m)]) {
                        const ImVec2 bc = bubbles[static_cast<size_t>(m)];
                        const float d = std::hypot(bc.x - io.MousePos.x, bc.y - io.MousePos.y);
                        if (d < bestD) {
                            bestD = d;
                            best = m;
                        }
                    }
                }
                if (best >= 0) {
                    Keyframe k = stageView.markChoice.pending;
                    k.mark = best;
                    r.keyframeJuggler = stageView.markChoice.juggler;
                    r.keyframe = k;
                    r.loiterJuggler = stageView.markChoice.juggler;
                    r.loiterArrival = k;
                    r.loiterAt = stageView.markChoice.at;
                }
                stageView.markChoice.active = false;
            }
            if (editingFloor && ImGui::IsItemActivated() && leftPress && !stageView.pressTakenByChoice) {
                if (markUnderMouse >= 0 && (underMouse < 0 || markRing)) {
                    stageView.markDrag = markUnderMouse;
                    stageView.markTurning = onArrow;
                    stageView.markForKeyframe = markRing;
                    // Pressed: it's in the selection now (on its own, or added with Shift), so
                    // a drag moves the selection.
                    stageView.markPressWasSelected =
                        std::find(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                                  stageView.markDrag) != stageView.selectedMarks.end();
                    if (!stageView.markPressWasSelected) {
                        if (!io.KeyShift) stageView.selectedMarks.clear();
                        stageView.selectedMarks.push_back(stageView.markDrag);
                    }
                    stageView.markDragStart.clear();
                    for (const int m : stageView.selectedMarks)
                        stageView.markDragStart.push_back({doc.choreography.marks[static_cast<size_t>(m)].x,
                                                 doc.choreography.marks[static_cast<size_t>(m)].z});
                } else if (underMouse >= 0 && !loop.empty() && doc.choreography.link(underMouse).active()) {
                    // A juggler who walks like another goes where the leader goes.
                    r.note = "J" + std::to_string(underMouse + 1) + " " +
                             walkLinkLabel(doc.choreography.link(underMouse)) + ": move J" +
                             std::to_string(doc.choreography.link(underMouse).leader + 1) + ", or unlink J" +
                             std::to_string(underMouse + 1) + " (right-click them: Walk Like...).";
                    r.noteSeconds = 5.0;
                } else if (underMouse >= 0 && !loop.empty() &&
                           inRotatedRepeat(doc, loop, std::lround(playback.beat), &r.note)) {
                    // (Not in a rotated repeat: the note says where to do it.)
                    r.noteSeconds = 6.0;
                } else if (underMouse >= 0 && !loop.empty()) {
                    // A juggler: dragging them puts them somewhere new (a keyframe).
                    stageView.jugglerDrag = underMouse;
                    Vec3 at;
                    jugglerPlacementAt(loop, doc.juggleParams, underMouse, std::round(playback.beat), &at,
                                       &stageView.jugglerDragYaw);
                }
            }
            if (stageView.jugglerDrag >= 0 && !ImGui::IsItemActive()) {
                // Dropped: how long do they stay? (See applyStageRequest.)
                if (ImGui::IsItemDeactivated() && dragged && stageView.jugglerDrag < loop.jugglers &&
                    !loop.empty()) {
                    r.droppedJuggler = stageView.jugglerDrag;
                    r.dropAt = io.MousePos;
                    r.dropOnFloor = floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize,
                                                    io.MousePos, &r.dropFloor);
                }
                stageView.jugglerDrag = -1;
            }
            if (stageView.jugglerDrag >= 0 && stageView.jugglerDrag < loop.jugglers && dragged) {
                // Dragged to the floor under the mouse, on the current beat: onto a spike mark
                // if it's dropped on one (facing as the mark says), else a spot (snapped to the
                // floor grid; Shift: freely), facing as they were, which gets a mark of its own
                // when they're let go.
                Vec3 floor;
                if (floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize, io.MousePos,
                                    &floor)) {
                    const int cycle = std::max(1, choreographyCycle(loop, doc.juggleParams));
                    const long beat = std::lround(playback.beat);
                    Keyframe k;
                    k.beat = static_cast<int>(((beat % cycle) + cycle) % cycle);
                    for (const Keyframe& old :
                         doc.choreography.keys.size() > static_cast<size_t>(stageView.jugglerDrag)
                             ? doc.choreography.keys[static_cast<size_t>(stageView.jugglerDrag)]
                             : std::vector<Keyframe>())
                        if (old.beat == k.beat) k.longTurn = old.longTurn;
                    for (int m = 0; m < static_cast<int>(doc.choreography.marks.size()); ++m) {
                        const SpikeMark& sm = doc.choreography.marks[static_cast<size_t>(m)];
                        if (std::hypot(floor.x - sm.x * markScale, floor.z - sm.z * markScale) <= kMarkRadius * markScale)
                            k.mark = m;
                    }
                    if (k.mark < 0) {
                        float x = floor.x, z = floor.z;
                        if (!io.KeyShift) snapToFloorGrid(settings, &x, &z);
                        k.x = x / markScale;
                        k.z = z / markScale;
                        k.yaw = stageView.jugglerDragYaw;
                    }
                    r.keyframeJuggler = stageView.jugglerDrag;
                    r.keyframe = k;
                    r.pause = true;
                    r.goToBeat = true;
                    r.beat = static_cast<double>(beat);
                    stageView.selectedJuggler = stageView.jugglerDrag;
                }
            }
            if (stageView.markDrag >= 0 && stageView.markDrag < static_cast<int>(doc.choreography.marks.size()) &&
                ImGui::IsItemActive() && dragged) {
                Vec3 floor;
                if (floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize, io.MousePos,
                                    &floor)) {
                    SpikeMark m = doc.choreography.marks[static_cast<size_t>(stageView.markDrag)];
                    if (stageView.markTurning) {
                        // Turned to point at the mouse: in 15-degree steps (Shift: freely). With
                        // the radial grid, the steps are counted from facing the middle, so one of
                        // them is along the mark's spoke, whatever the spokes.
                        float yaw = std::atan2(floor.x - m.x * markScale, floor.z - m.z * markScale);
                        if (!io.KeyShift) {
                            const float step = kPi / 12.0f;
                            const float from = settings.floorGrid == FloorGridType::Radial && std::hypot(m.x, m.z) > 1e-3f
                                                   ? std::atan2(-m.x, -m.z)
                                                   : 0.0f;
                            yaw = from + std::round((yaw - from) / step) * step;
                        }
                        m.yaw = yaw;
                        r.movedMarks.push_back({stageView.markDrag, m});
                    } else {
                        // Moved to the mouse: snapped to the floor grid (Shift: freely); the rest
                        // of the selection moves with it, keeping their shape.
                        float x = floor.x, z = floor.z;
                        if (!io.KeyShift) snapToFloorGrid(settings, &x, &z);
                        const auto here = std::find(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                                                    stageView.markDrag);
                        const size_t me = static_cast<size_t>(here - stageView.selectedMarks.begin());
                        if (here == stageView.selectedMarks.end() ||
                            stageView.markDragStart.size() != stageView.selectedMarks.size()) {
                            m.x = x / markScale;
                            m.z = z / markScale;
                            r.movedMarks.push_back({stageView.markDrag, m});
                        } else {
                            const float dx = x / markScale - stageView.markDragStart[me].first;
                            const float dz = z / markScale - stageView.markDragStart[me].second;
                            for (size_t i = 0; i < stageView.selectedMarks.size(); ++i) {
                                const int k = stageView.selectedMarks[i];
                                if (k < 0 || k >= static_cast<int>(doc.choreography.marks.size())) continue;
                                SpikeMark moved = doc.choreography.marks[static_cast<size_t>(k)];
                                moved.x = stageView.markDragStart[i].first + dx;
                                moved.z = stageView.markDragStart[i].second + dz;
                                r.movedMarks.push_back({k, moved});
                            }
                        }
                    }
                }
            }
            const bool cameraDrag =
                ImGui::IsItemActive() && stageView.markDrag < 0 && stageView.jugglerDrag < 0 &&
                stageView.viewPress >= 0 && (stageView.viewPress != 0 || stageView.viewOrbitLeft) &&
                ImGui::IsMouseDragging(static_cast<ImGuiMouseButton>(stageView.viewPress), 0.0f);
            const bool orbiting = cameraDrag && !stageView.viewPan;
            const bool panning = cameraDrag && stageView.viewPan;
            if (orbiting) {
                const ImVec2 d = io.MouseDelta;
                // In the top view, orbiting turns the view round the vertical (it stays
                // looking straight down).
                if (stageView.camera.free) {
                    if (!stageView.cameraOrbiting)
                        stageView.orbitCenter =
                            freeOrbitCenter(stageView.lastCamera, scene, stageView.selectedJuggler, stageBoxFor(scene, doc, loop));
                    orbitFreeCamera(stageView.camera, stageView.orbitCenter, -d.x * 0.008f, d.y * 0.008f);
                } else {
                    stageView.camera.yaw -= d.x * 0.008f;
                    if (!stageView.camera.topView)
                        stageView.camera.pitch = std::clamp(stageView.camera.pitch + d.y * 0.008f, -1.45f, 1.45f);
                }
            }
            if (panning) {
                // Panning drags the floor under the mouse along with it (and frees the camera).
                if (!stageView.camera.free) makeCameraFree(stageView.camera, stageView.lastCamera);
                if (!stageView.cameraPanning) {
                    Vec3 floor;
                    stageView.panDepth = floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin,
                                                         viewSize, io.MousePos, &floor)
                                             ? std::min(length(floor - stageView.lastCamera.position), 40.0f)
                                             : length(freeOrbitCenter(stageView.lastCamera, scene,
                                                                      stageView.selectedJuggler, stageBoxFor(scene, doc, loop)) -
                                                      stageView.lastCamera.position);
                }
                const float metersPerPixel =
                    2.0f * stageView.panDepth * std::tan(kCameraFovY * 0.5f) / std::max(1.0f, viewSize.y);
                Vec3 forward, right, up;
                freeCameraAxes(stageView.camera, &forward, &right, &up);
                stageView.camera.freePos = stageView.camera.freePos - right * (io.MouseDelta.x * metersPerPixel) +
                                 up * (io.MouseDelta.y * metersPerPixel);
            }
            stageView.cameraOrbiting = orbiting;
            stageView.cameraPanning = panning;
            // A click is a press and release without dragging (dragging orbits).
            if (stageView.pathPress >= 0 && stageView.pathPress < static_cast<int>(paths.points.size()) &&
                ImGui::IsItemDeactivated() && leftPress && !dragged) {
                // A click on a path point: its beat (in the cycle playing now), its juggler selected.
                const PathPoint& point = paths.points[static_cast<size_t>(stageView.pathPress)];
                if (loop.jugglers > 1) stageView.selectedJuggler = point.juggler;
                r.pause = true;
                r.goToBeat = true;
                r.beat = std::floor(playback.beat / frame.pathCycle) * frame.pathCycle + point.firstBeat;
                stageView.markDrag = -1;
                stageView.jugglerDrag = -1;
                stageView.pathPress = -1;
            } else if (stageView.markDrag >= 0 && ImGui::IsItemDeactivated() && leftPress && !dragged &&
                       !stageView.markForKeyframe) {
                stageView.selectedJuggler = underMouse;  // a click on someone standing on a mark
                stageView.markDrag = -1;
            } else if (stageView.markDrag >= 0 && ImGui::IsItemDeactivated()) {
                if (!dragged) {
                    // A click on a mark selects it (Shift: adds it, or takes it away if it was
                    // already in), and no juggler. (A keyframe: drag a juggler onto it.)
                    if (io.KeyShift) {
                        if (stageView.markPressWasSelected)
                            stageView.selectedMarks.erase(
                                std::remove(stageView.selectedMarks.begin(), stageView.selectedMarks.end(),
                                            stageView.markDrag),
                                stageView.selectedMarks.end());
                    } else {
                        stageView.selectedMarks.assign(1, stageView.markDrag);
                    }
                    stageView.selectedJuggler = -1;
                    stageView.frameSelected = false;
                }
                stageView.markDrag = -1;
            } else if (canSelect && ImGui::IsItemDeactivated() && leftPress && !dragged &&
                       !stageView.pressTakenByChoice) {
                stageView.selectedJuggler = underMouse;
                stageView.selectedMarks.clear();
                if (underMouse < 0) stageView.frameSelected = false;
            }
            // Right-click (without dragging) the floor: add a mark there, or (on a mark) its menu.
            if (editingFloor && ImGui::IsItemDeactivated() && stageView.viewPress == 1 && !dragged) {
                stageView.markMenuMark = markRing ? markUnderMouse : -1;
                Vec3 floor(0.0f, 0.0f, 0.0f);
                if (stageView.markMenuMark >= 0 || underMouse >= 0 ||
                    floorUnderMouse(stageView.lastCamera, stageView.lastAspect, viewMin, viewSize, io.MousePos,
                                    &floor)) {
                    stageView.markMenuPoint = Vec3(floor.x / markScale, 0.0f, floor.z / markScale);
                    stageView.floorMenuJuggler = underMouse;
                    ImGui::OpenPopup("##floor_menu");
                }
            }
            if (ImGui::BeginPopup("##floor_menu")) {
                if (stageView.floorMenuJuggler >= 0 && stageView.floorMenuJuggler < loop.jugglers &&
                    loop.jugglers > 1) {
                    const std::string how = walkLinkLabel(doc.choreography.link(stageView.floorMenuJuggler));
                    ImGui::TextDisabled("Juggler %d%s%s", stageView.floorMenuJuggler + 1, how.empty() ? "" : ": ",
                                        how.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Walk Like...")) r.walkLikeJuggler = stageView.floorMenuJuggler;
                    ImGui::Separator();
                }
                if (stageView.markMenuMark >= 0 &&
                    stageView.markMenuMark < static_cast<int>(doc.choreography.marks.size())) {
                    ImGui::TextDisabled(
                        "Spike mark %s",
                        doc.choreography.marks[static_cast<size_t>(stageView.markMenuMark)].name.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Face the Middle")) {
                        SpikeMark m = doc.choreography.marks[static_cast<size_t>(stageView.markMenuMark)];
                        if (std::hypot(m.x, m.z) > 1e-3f) {
                            m.yaw = std::atan2(-m.x, -m.z);
                            r.movedMarks.push_back({stageView.markMenuMark, m});
                        }
                    }
                    if (ImGui::MenuItem("Rename...")) r.renameMark = stageView.markMenuMark;
                    if (ImGui::MenuItem("Delete Spike Mark")) r.deleteMark = stageView.markMenuMark;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Only a mark nobody stands on (the others keep their names).");
                    if (ImGui::MenuItem("Add Another Spike Mark Here")) {
                        // Same place, facing the other way for now (so its arrow and name are
                        // clear of the first's), and selected, ready to be turned by its arrow.
                        r.addMark = true;
                        r.newMark = doc.choreography.marks[static_cast<size_t>(stageView.markMenuMark)];
                        r.newMark.name.clear();
                        r.newMark.yaw = std::remainder(r.newMark.yaw + kPi, 2.0f * kPi);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("A second mark on the same spot, facing the other way and selected: drag its\n"
                                          "arrow to turn it. Putting a juggler there then asks which mark they stand on.");
                } else if (stageView.floorMenuJuggler < 0) {
                    if (ImGui::MenuItem("Add Spike Mark Here")) {
                        SpikeMark m;
                        float x = stageView.markMenuPoint.x * markScale, z = stageView.markMenuPoint.z * markScale;
                        snapToFloorGrid(settings, &x, &z);
                        m.x = x / markScale;
                        m.z = z / markScale;
                        m.yaw = std::hypot(m.x, m.z) > 1e-3f ? std::atan2(-m.x, -m.z) : 0.0f;  // facing the middle
                        r.addMark = true;  // (selected, to see which it is and turn it)
                        r.newMark = m;
                    }
                }
                ImGui::EndPopup();
            }
            // Things that can be dragged show it: a hand over a juggler (in choreography).
            if (editingFloor && stageView.jugglerDrag >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            else if (editingFloor && underMouse >= 0 && stageView.markDrag < 0 && !orbiting)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (editingFloor && (markUnderMouse >= 0 || stageView.markDrag >= 0))
                ImGui::SetMouseCursor(stageView.markTurning && stageView.markDrag >= 0 ? ImGuiMouseCursor_ResizeAll
                                                                                       : ImGuiMouseCursor_Hand);
            if (r.hoveredPathPoint >= 0 && !ImGui::IsItemActive()) {
                const PathPoint& point = paths.points[static_cast<size_t>(r.hoveredPathPoint)];
                const std::string beats = pathPointBeats(point, frame.pathCycle);
                ImGui::SetTooltip("J%d, beat%s %s\nClick to go to %s, J%d selected", point.juggler + 1,
                                  point.beats > 1 ? "s" : "", beats.c_str(),
                                  point.beats > 1 ? "its first beat" : "that beat", point.juggler + 1);
            } else if (editingFloor && markUnderMouse >= 0 && !ImGui::IsItemActive() && (markRing || onArrow)) {
                if (onArrow)
                    ImGui::SetTooltip("Spike mark %s: drag the arrow to turn it (Shift: freely)",
                                      doc.choreography.marks[static_cast<size_t>(markUnderMouse)].name.c_str());
                else
                    ImGui::SetTooltip("Spike mark %s: drag to move it (it snaps to the grid; Shift: freely)\n"
                                      "Click to select it, Shift+click to add it to the selection: dragging one\n"
                                      "moves them all. To put a juggler here, drag them onto it.\n"
                                      "Right-click for more",
                                      doc.choreography.marks[static_cast<size_t>(markUnderMouse)].name.c_str());
            }
            if (canSelect && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                stageView.selectedJuggler = underMouse;
                stageView.frameSelected = underMouse >= 0;
                stageView.camera.free = false;
            }
            if (ImGui::IsItemHovered()) {
                if (io.MouseWheel != 0.0f && stageView.camera.free) {
                    // A free camera moves along its view, more slowly the nearer it is to
                    // something.
                    Vec3 forward, right, up;
                    freeCameraAxes(stageView.camera, &forward, &right, &up);
                    stageView.camera.freePos =
                        stageView.camera.freePos +
                        forward * (io.MouseWheel * 0.15f * nearestToCamera(scene, doc, loop, stageView.camera.freePos));
                } else if (io.MouseWheel != 0.0f) {
                    stageView.camera.zoom =
                        std::clamp(stageView.camera.zoom * std::pow(0.88f, io.MouseWheel), 0.15f, 6.0f);
                }
                if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home)) {
                    resetCamera(stageView.camera);
                    stageView.frameSelected = false;
                }
                // Paused, resting on a club or ring in the air: its throw and spin rate.
                bool propTip = false;
                if (!playback.playing && scene.prop != PropType::Ball && !ImGui::IsItemActive() && r.hoveredPathPoint < 0) {
                    const BallState* nearest = nullptr;
                    float nearestD = 22.0f;  // pixels
                    for (const BallState& b : scene.balls) {
                        ImVec2 at;
                        if (!b.inFlight || b.flightBeats <= 0.0f ||
                            !projectToScreen(stageView.lastViewProj, b.center, viewMin, viewSize, &at))
                            continue;
                        const float d = std::hypot(at.x - io.MousePos.x, at.y - io.MousePos.y);
                        if (d < nearestD) {
                            nearestD = d;
                            nearest = &b;
                        }
                    }
                    if (nearest) {
                        static const char* const kNames[] = {"flat (no spin)", "a single", "a double", "a triple", "a quad"};
                        const std::string spinName = nearest->spins >= 0 && nearest->spins <= 4
                                                         ? std::string(kNames[nearest->spins])
                                                         : std::to_string(nearest->spins) + " spins";
                        const std::string who = nearest->thrower == nearest->catcher
                                                    ? std::string()
                                                    : ", J" + std::to_string(nearest->thrower + 1) + " to J" +
                                                          std::to_string(nearest->catcher + 1);
                        ImGui::SetTooltip("A %d%s, %s\n%.2f spins a beat (%d in %.2f beats in the air)",
                                          nearest->throwValue, who.c_str(), spinName.c_str(),
                                          static_cast<double>(nearest->spins) / static_cast<double>(nearest->flightBeats),
                                          nearest->spins, static_cast<double>(nearest->flightBeats));
                        propTip = true;
                    }
                }
                // A hint, only after the mouse has rested here a moment.
                if (!propTip && r.hoveredPathPoint < 0 && !ImGui::IsItemActive() && (markUnderMouse < 0 || underMouse >= 0) &&
                    !(markUnderMouse >= 0 && markRing) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    if (underMouse >= 0 && editingFloor)
                        ImGui::SetTooltip("Juggler %d\nClick to select, double-click to frame\n"
                                          "Drag to put them somewhere else on this beat (a keyframe; drop on a mark to use it)",
                                          underMouse + 1);
                    else if (underMouse >= 0)
                        ImGui::SetTooltip("Juggler %d\nClick to select, double-click to frame", underMouse + 1);
                    else if (canSelect)
                        ImGui::SetTooltip("Right-drag (or Alt+drag) to orbit, middle-drag or Shift+right-drag to pan, wheel to zoom, Home to reset\n"
                                          "Click a juggler to select them, double-click to frame them");
                    else
                        ImGui::SetTooltip("Right-drag (or Alt+drag) to orbit, middle-drag or Shift+right-drag to pan, wheel to zoom, Home to reset");
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return r;
}

namespace {

// A 3D mouse axis's reading (raw counts) as a rate by its response curve (see response_curve.h),
// as a percentage of the axis's full push, with the reading's sign.
float spaceRate(float reading, float fullPush, const ResponseCurve& curve) {
    return std::copysign(responseRate(curve, std::fabs(reading) * 100.0f / std::max(fullPush, 1.0f)), reading);
}

}  // namespace

CameraView updateStageCamera(StageView& stageView, const DocumentState& doc, const StageState& stage,
                             const AppSettings& settings, const StageFrame& frame, const SpaceMouseState& spaceMouse,
                             bool loiterOpen) {
    const ImGuiIO& io = ImGui::GetIO();
    const JugglerScene& scene = frame.scene;
    const JugglingLoop& loop = frame.loop;
    const ImVec2 viewSize = frame.viewSize;
    const float jugglerAspect = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
    // The top view frames the whole floor in use: the jugglers and the spike marks. (Not
    // while a mark is being dragged: the view would shift under the mouse.)
    // While a juggler or mark is being moved (or the loiter popup is up), it doesn't shrink,
    // and grows only slowly (pushing something against the edge widens the view gently), so
    // the floor doesn't shift under the mouse.
    {
        float half = 1.5f;
        for (const JugglerState& js : scene.jugglers) half = std::max(half, std::max(std::fabs(js.position.x), std::fabs(js.position.z)) + 0.8f);
        const float markScale = choreographyScale(doc, loop);
        for (const SpikeMark& m : doc.choreography.marks)
            half = std::max(half, std::max(std::fabs(m.x), std::fabs(m.z)) * markScale + 0.8f);
        const bool placing = stageView.jugglerDrag >= 0 || stageView.markDrag >= 0 || loiterOpen;
        if (!placing)
            stageView.camera.floorHalf = half;
        else if (half > stageView.camera.floorHalf)
            stageView.camera.floorHalf +=
                std::min(half - stageView.camera.floorHalf, 0.5f * io.DeltaTime);  // 0.5 m a second
    }
    // The 3D mouse, grabbing the stage: spin and tilt the cap to orbit; push it away to move
    // back (zoom out), pull it toward you to move in; slide it sideways or up and down and
    // the stage goes with it (which frees the camera). Every axis acts at once, so one
    // movement can pull back, lift and tilt together. (The cap's axes leak into each other a
    // little, a tilt reading as a bit of a push, say; a hand corrects for that without
    // thinking, so nothing here tries to.)
    if (spaceMouse.present) {
        const float step = io.DeltaTime;
        // Each axis by its response curve (Edit > Preferences): slides in pixels a second (made
        // meters below, at the orbit center's distance), turns in degrees a second.
        const float* full = settings.spaceFullPush;
        const float slideX = spaceRate(spaceMouse.move[0], full[0], settings.spaceSlideCurve),
                    slideY = spaceRate(spaceMouse.move[1], full[1], settings.spaceSlideCurve),
                    slideZ = spaceRate(spaceMouse.move[2], full[2], settings.spaceSlideCurve);
        const float spin = spaceRate(spaceMouse.turn[2], full[5], settings.spaceTurnCurve) * kPi / 180.0f;
        const float tilt = spaceRate(spaceMouse.turn[0], full[3], settings.spaceTurnCurve) * kPi / 180.0f;
        const bool turning = spin != 0.0f || tilt != 0.0f;
        const bool sliding = slideX != 0.0f || slideY != 0.0f || slideZ != 0.0f;
        // Anything done with the 3D mouse makes the camera free, from where it is.
        if (!stageView.camera.free && (turning || sliding)) {
            makeCameraFree(stageView.camera, stageView.lastCamera);
            stageView.spaceOrbitUntil = 0.0;
        }
        if (stageView.camera.free) {
            const double now = ImGui::GetTime();
            if (spin != 0.0f || tilt != 0.0f) {
                // Spinning and tilting turn the stage like an object in your hand: the
                // camera orbits round a center that stays put while the cap keeps turning
                // (and a moment after), round the vertical for a spin and over the top for
                // a tilt (never past straight down or up, so it never turns upside down).
                if (now >= stageView.spaceOrbitUntil)
                    stageView.orbitCenter =
                        freeOrbitCenter(stageView.lastCamera, scene, stageView.selectedJuggler, stageBoxFor(scene, doc, loop));
                stageView.spaceOrbitUntil = now + 0.3;
                orbitFreeCamera(stageView.camera, stageView.orbitCenter, -spin * step, tilt * step);
            }
            if (sliding) {
                Vec3 forward, right, up;
                freeCameraAxes(stageView.camera, &forward, &right, &up);
                // Pixels a second on the screen at the distance of what the camera orbits about, so
                // the stage moves as fast on the screen however near or far it is. (Pushing and
                // pulling go at the same speed.)
                const Vec3 center =
                    now < stageView.spaceOrbitUntil
                        ? stageView.orbitCenter
                        : freeOrbitCenter(stageView.lastCamera, scene, stageView.selectedJuggler,
                                          stageBoxFor(scene, doc, loop));
                const float distance = std::max(length(stageView.camera.freePos - center), 0.3f);
                const float metersPerPixel =
                    2.0f * distance * std::tan(kCameraFovY * 0.5f) / std::max(viewSize.y, 1.0f);
                const float v = metersPerPixel * step;
                const Vec3 across = right * (-slideX * v) + up * (-slideZ * v);
                stageView.camera.freePos = stageView.camera.freePos + across - forward * (slideY * v);
                // While a turn is going on, what it turns about moves across with the camera, so
                // it stays straight ahead (pulling back or pushing in moves along that line
                // anyway): sliding while turning swings the view smoothly, rather than round a
                // point that's drifted off to the side.
                if (now < stageView.spaceOrbitUntil) stageView.orbitCenter = stageView.orbitCenter + across;
            }
        }
    }
    const CameraView cameraView = updateCamera(stageView.camera, stage.extents, jugglerAspect, io.DeltaTime);
    stageView.lastViewProj = cameraViewProj(cameraView, jugglerAspect);
    stageView.lastCamera = cameraView;
    stageView.lastAspect = jugglerAspect;
    return cameraView;
}

void drawStageOverlays(StageView& stageView, const DocumentState& doc, const StageState& stage,
                       const AppSettings& settings, const StageFrame& frame, const CameraView& cameraView,
                       int hoveredPathPoint, const std::vector<int>& styleOfBall) {
    const JugglerScene& scene = frame.scene;
    const JugglingLoop& loop = frame.loop;
    const ChoreographyPaths& paths = frame.paths;
    const ImVec2 viewMin = frame.viewMin;
    const ImVec2 viewSize = frame.viewSize;
    // Floor overlays: their lines go in the 3D pass (overlayTriangles, drawn after the scene,
    // so bodies hide them); their text, here, where no body is in front of it.
    stageView.overlayTriangles.clear();
    {
        OverlayBuilder overlay(&stageView.overlayTriangles, cameraView, viewSize.y);
        const BodyOccluder bodies(scene, cameraView.position);
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        if (stageView.choreographyMode && doc.choreography.active()) {
            const StageBox box = stageBoxFor(scene, doc, loop);
            buildFloorGrid(overlay, settings, box.min.x, box.min.z, box.max.x, box.max.z);
            std::vector<int> lit = stageView.markChoice.active ? stageView.markChoice.marks : std::vector<int>();
            lit.insert(lit.end(), stageView.selectedMarks.begin(), stageView.selectedMarks.end());
            buildSpikeMarks(overlay, doc.choreography, choreographyScale(doc, loop), stageView.markDrag, lit);
            drawSpikeMarkNumbers(bg, doc.choreography, choreographyScale(doc, loop), stageView.lastViewProj, viewMin,
                                 viewSize, bodies, lit, markGroupView(stageView, doc));
        }
        if (frame.showPaths) {
            const int pathSelected = loop.jugglers == 1 ? 0 : stageView.selectedJuggler;
            buildChoreographyPaths(overlay, paths, pathSelected, hoveredPathPoint);
            drawChoreographyPathLabels(bg, paths, frame.pathCycle, stageView.lastViewProj, viewMin, viewSize,
                                       pathSelected, hoveredPathPoint, settings.pathBeatNumbers, bodies);
        }
        // The reticle: what the camera orbits about. Always shown (faintly) for a free
        // camera, brighter while orbiting; for the automatic framing, only while orbiting.
        const bool orbitingNow = stageView.cameraOrbiting || ImGui::GetTime() < stageView.spaceOrbitUntil;
        if (stageView.camera.free || orbitingNow) {
            const Vec3 center =
                !stageView.camera.free
                    ? cameraView.target
                    : (orbitingNow ? stageView.orbitCenter
                                   : freeOrbitCenter(cameraView, scene, stageView.selectedJuggler, stageBoxFor(scene, doc, loop)));
            buildOrbitReticle(overlay, center, orbitingNow);
        }
    }
    // The pattern has a problem: the stage is as it last was that worked, tinted red round the edge.
    if (stage.broken) {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const ImVec2 b(viewMin.x + viewSize.x, viewMin.y + viewSize.y);
        bg->AddRectFilled(viewMin, b, IM_COL32(200, 40, 30, 22));
        for (int i = 0; i < 6; ++i) {
            const float inset = static_cast<float>(i) * 3.0f;
            bg->AddRect(ImVec2(viewMin.x + inset, viewMin.y + inset), ImVec2(b.x - inset, b.y - inset),
                        IM_COL32(220, 50, 40, 150 - i * 24), 0.0f, 0, 3.0f);
        }
        const bool throwing = stage.derived.problem(DocumentProblemKind::ThrowingLoop) != nullptr;
        const bool walking = stage.derived.problem(DocumentProblemKind::WalkingLoop) != nullptr;
        const char* says =
            stage.derived.problem(DocumentProblemKind::Throws)
                ? "The throws have an error (see the grid): this is the pattern as it last worked"
            : throwing ? (walking ? "Walking and throwing loops: see the grid" : "Throwing loop: see the grid")
                       : "Walking loop: see the grid";
        bg->AddText(ImVec2(viewMin.x + 24.0f, b.y - ImGui::GetTextLineHeight() - 20.0f), IM_COL32(255, 140, 120, 255), says);
    }
    drawJugglerLabels(ImGui::GetBackgroundDrawList(), scene, stageView.lastViewProj, viewMin, viewSize,
                      stageView.selectedJuggler, &stageView.jugglerLabelCenters);
    if (settings.showThrowValues)
        drawPropValueLabels(ImGui::GetBackgroundDrawList(), scene, stageView.lastViewProj, viewMin, viewSize,
                            settings.colorVision, styleOfBall, doc.parsed.form.relative);
}
