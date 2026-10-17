// app.cpp - JuggleSim itself (see app.h): the panes, editing, playback and drawing.
//
// Layout (below the main menu bar):
//   +----------------------+----------------------+
//   |                      |                      |
//   |    ladder diagram    |   3D juggler view    |
//   |                      |  (plain GL viewport) |
//   +----------------------+                      |
//   |  siteswap entry      |                      |
//   +----------------------+----------------------+
// The divider between the two halves can be dragged.

#include "app.h"

#include "color_vision.h"
#include "gl_funcs.h"
#include "juggle_sim.h"
#include "juggler_figure.h"
#include "ladder_view.h"
#include "loop_ops.h"
#include "math3d.h"
#include "mesh.h"
#include "pattern.h"
#include "pattern_library.h"
#include "pattern_library_ui.h"
#include "platform.h"
#include "prop_figure.h"
#include "renderer.h"
#include "settings.h"
#include "choreography.h"
#include "siteswap.h"
#include "siteswap_edit.h"
#include "siteswap_grid.h"
#include "sound.h"

#include "imgui.h"
#include "imgui_internal.h"  // to open a slider's text field on double-click (fineSlider)
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

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

// Pixel rectangle in GL convention (origin at the bottom-left of the window).
struct GLRect {
    int x, y, w, h;
};

// ---- Camera -------------------------------------------------------------------------------
//
// By default the camera frames the pattern: it looks at the juggler from the front (they face
// +Z) with the bottom of the view just below the lowest point the hands reach and the top just
// above the highest point any ball reaches. The user can orbit (drag) and zoom (wheel); those
// are kept as offsets from the automatic framing, so changing patterns re-frames from wherever
// the user is looking. Home (over the juggler) or View > Reset Camera clears them.
//
// With several jugglers the automatic framing takes in all of them (Frame All), or just the
// selected one (Frame Selected: their hands, what they hold and their selfs). Either way the
// camera keeps the user's orbit; it only moves what it looks at and how far away it is.
//
// Zooming in keeps the bottom of the view where it is (just below the lowest point the hands
// reach) and crops from the top, so the juggler stays in view and the flights are what get cut
// off. Zoomed in further than the juggler's own height (from that low point to the top of the
// head), the view stays centered on the juggler.

constexpr float kCameraFovY = 35.0f * kPi / 180.0f;
constexpr float kJugglerTopY = 1.80f;  // a little above the top of the juggler's head
// A ring of 3+ jugglers is seen from a little above, so the near ones don't hide the far ones.
constexpr float kRingPitch = 0.50f;   // radians (about 29 degrees)
constexpr float kRingMargin = 1.15f;  // extra distance, so the nearest jugglers fit

struct CameraControl {
    // User offsets.
    float yaw = 0.0f;    // radians; positive swings the camera toward the juggler's left (+X)
    float pitch = 0.0f;  // radians; positive looks down from above
    float zoom = 1.0f;   // distance multiplier; < 1 is closer
    // Automatic framing, eased toward the pattern's extents so changes glide instead of jumping.
    bool framed = false;
    float centerX = 0.0f;
    float centerY = 1.4f;
    float centerZ = 0.36f;
    float bottomY = 0.8f;  // bottom edge of the full framing
    float distance = 3.0f;
    float basePitch = 0.0f;  // looking down a little at a ring of 3+ jugglers (eased)
    // Top view (T): looking (almost) straight down at the floor, framing floorHalf meters either
    // side of the middle. topBlend eases between the two views.
    bool topView = false;
    float topBlend = 0.0f;
    float floorHalf = 2.0f;
    // Free camera: once the user moves the camera itself (panning, or anything with a 3D
    // mouse), it stays exactly where they put it, whatever the pattern does,
    // until they re-frame (Home, Frame All, F, a double-click, T, or loading a pattern without a
    // camera of its own). Its position, and its direction as yaw and pitch (the same angles as
    // above: the direction from what it looks at back to the camera).
    bool free = false;
    Vec3 freePos{0.0f, 1.6f, 4.0f};
    float freeYaw = 0.0f;
    float freePitch = 0.0f;
};

// A free camera can look all the way down (or up), but not past it: it never turns upside
// down. (A hair short of straight, so its sideways direction stays defined.)
constexpr float kMaxFreePitch = 0.5f * kPi - 0.002f;

// The direction from what the camera looks at back to the camera, for a yaw and pitch.
Vec3 cameraBackDir(float yaw, float pitch) {
    return Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

struct CameraView {
    Vec3 position;
    Vec3 target;
    float farPlane;
};

void resetCamera(CameraControl& c) {
    c.yaw = 0.0f;
    c.pitch = 0.0f;
    c.zoom = 1.0f;
    c.free = false;
}

// Makes the camera free, starting exactly where `view` (as last drawn) has it.
void makeCameraFree(CameraControl& c, const CameraView& view) {
    const Vec3 back = normalize(view.position - view.target);
    c.freePos = view.position;
    c.freePitch = std::asin(std::clamp(back.y, -1.0f, 1.0f));
    c.freeYaw = std::atan2(back.x, back.z);
    c.free = true;
    c.topView = false;
    c.topBlend = 0.0f;
}

// Orbits the free camera about `center`: round the vertical through it by dyaw, and up or down
// by dpitch (positive: from higher up), turning the view with it, so the center stays where it
// is on screen.
void orbitFreeCamera(CameraControl& c, Vec3 center, float dyaw, float dpitch) {
    const float newPitch = std::clamp(c.freePitch + dpitch, -kMaxFreePitch, kMaxFreePitch);
    dpitch = newPitch - c.freePitch;
    Vec3 offset = c.freePos - center;
    // Round the vertical.
    const float cy = std::cos(dyaw), sy = std::sin(dyaw);
    offset = Vec3(offset.x * cy + offset.z * sy, offset.y, offset.z * cy - offset.x * sy);
    const float yaw = c.freeYaw + dyaw;
    // Up or down: round the camera's own sideways axis (Rodrigues' rotation, by -dpitch).
    const Vec3 axis(std::cos(yaw), 0.0f, -std::sin(yaw));
    const float a = -dpitch, ca = std::cos(a), sa = std::sin(a);
    offset = offset * ca + cross(axis, offset) * sa + axis * (dot(axis, offset) * (1.0f - ca));
    c.freePos = center + offset;
    c.freeYaw = yaw;
    c.freePitch = newPitch;
}

// The free camera's axes: forward (where it looks), right, and up.
void freeCameraAxes(const CameraControl& c, Vec3* forward, Vec3* right, Vec3* up) {
    *forward = -cameraBackDir(c.freeYaw, c.freePitch);
    *right = normalize(cross(*forward, Vec3(0.0f, 1.0f, 0.0f)));
    *up = cross(*right, *forward);
}

// The stage: the floor the choreography uses (the jugglers, their spike marks and keyframe
// spots, and a margin), from the floor to a bit above head height. A free camera's orbit
// centers always lie inside it.
struct StageBox {
    Vec3 min{-1.5f, 0.0f, -1.5f};
    Vec3 max{1.5f, 2.5f, 1.5f};
    bool contains(Vec3 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    Vec3 clamped(Vec3 p) const {
        return Vec3(std::clamp(p.x, min.x, max.x), std::clamp(p.y, min.y, max.y), std::clamp(p.z, min.z, max.z));
    }
    Vec3 middle() const { return (min + max) * 0.5f; }
};

// Where a free camera (as drawn: `view`) orbits about: the selected juggler; else the juggler
// in the middle of the view; else the floor point there, if it's on the stage; else where the
// line of sight leaves the stage; else (looking away from it) the stage point nearest that line.
Vec3 freeOrbitCenter(const CameraView& view, const JugglerScene& scene, int selected, const StageBox& stage) {
    constexpr float kChest = 1.25f;
    if (selected >= 0 && selected < static_cast<int>(scene.jugglers.size())) {
        const Vec3 p = scene.jugglers[static_cast<size_t>(selected)].position;
        return Vec3(p.x, kChest, p.z);
    }
    const Vec3 o = view.position;
    const Vec3 d = normalize(view.target - view.position);
    // A juggler in the middle of the view: a ray against an upright cylinder round each one.
    float bestT = 1e9f;
    Vec3 best;
    for (const JugglerState& js : scene.jugglers) {
        const float radius = 0.3f;
        const float ox = o.x - js.position.x, oz = o.z - js.position.z;
        const float a = d.x * d.x + d.z * d.z;
        if (a < 1e-6f) continue;
        const float b = ox * d.x + oz * d.z;
        const float cc = ox * ox + oz * oz - radius * radius;
        const float disc = b * b - a * cc;
        if (disc < 0.0f) continue;
        const float t = (-b - std::sqrt(disc)) / a;
        const float y = o.y + d.y * t;
        if (t > 0.0f && t < bestT && y >= 0.0f && y <= 1.85f) {
            bestT = t;
            best = Vec3(js.position.x, kChest, js.position.z);
        }
    }
    if (bestT < 1e9f) return best;
    // The floor, on the stage.
    if (d.y < -1e-4f) {
        const float t = -o.y / d.y;
        const Vec3 hit = o + d * t;
        if (hit.x >= stage.min.x && hit.x <= stage.max.x && hit.z >= stage.min.z && hit.z <= stage.max.z)
            return Vec3(hit.x, 0.0f, hit.z);
    }
    // Where the line of sight leaves the stage (slabs).
    float tEnter = 0.0f, tExit = 1e9f;
    const float origin[3] = {o.x, o.y, o.z}, dir[3] = {d.x, d.y, d.z};
    const float lo[3] = {stage.min.x, stage.min.y, stage.min.z}, hi[3] = {stage.max.x, stage.max.y, stage.max.z};
    bool crosses = true;
    for (int i = 0; i < 3 && crosses; ++i) {
        if (std::fabs(dir[i]) < 1e-6f) {
            if (origin[i] < lo[i] || origin[i] > hi[i]) crosses = false;
            continue;
        }
        float t0 = (lo[i] - origin[i]) / dir[i], t1 = (hi[i] - origin[i]) / dir[i];
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit = std::min(tExit, t1);
        if (tEnter > tExit) crosses = false;
    }
    if (crosses && tExit > 0.0f) return stage.clamped(o + d * tExit);
    // Looking away from the stage: the stage point nearest the line of sight.
    const float t = std::max(0.0f, dot(stage.middle() - o, d));
    return stage.clamped(o + d * t);
}

// Advances the automatic framing toward `extents` and returns the camera to render with.
CameraView updateCamera(CameraControl& c, const SceneExtents& extents, float aspect, float dt) {
    // A ring of jugglers is framed down to the floor (the whole formation); otherwise from the
    // lowest the hands go.
    const float bottom = extents.ring ? 0.0f : extents.lowestHandY - 0.04f;
    const float top = extents.highestPropY + 0.06f;
    const float tanHalf = std::tan(kCameraFovY * 0.5f);
    const float fitHeight = 0.5f * (top - bottom) / tanHalf;
    const float fitWidth = (extents.halfWidth + 0.05f) / (tanHalf * aspect);
    // Fitted where the jugglers come nearest the camera (for a ring, well in front of where it
    // aims), so the near ones aren't cut off.
    // (Seen from above, the near jugglers are also lower and nearer than the depth the fit is
    // worked out at; kRingMargin covers that.)
    const float wantDistance = extents.ring ? kRingMargin * std::max(fitHeight, fitWidth) + (extents.nearZ - extents.centerZ)
                                            : std::max(fitHeight, fitWidth);
    const float wantCenterY = 0.5f * (bottom + top);
    const float wantPitch = extents.ring ? kRingPitch : 0.0f;
    const float ease = c.framed ? 1.0f - std::exp(-dt * 5.0f) : 1.0f;
    c.topBlend += ((c.topView ? 1.0f : 0.0f) - c.topBlend) * ease;
    if (!c.framed) {
        c.centerX = extents.centerX;
        c.centerY = wantCenterY;
        c.centerZ = extents.centerZ;
        c.bottomY = bottom;
        c.distance = wantDistance;
        c.basePitch = wantPitch;
        c.framed = true;
    } else {
        c.centerX += (extents.centerX - c.centerX) * ease;
        c.centerY += (wantCenterY - c.centerY) * ease;
        c.centerZ += (extents.centerZ - c.centerZ) * ease;
        c.bottomY += (bottom - c.bottomY) * ease;
        c.distance += (wantDistance - c.distance) * ease;
        c.basePitch += (wantPitch - c.basePitch) * ease;
    }
    CameraView v;
    const float d = c.distance * c.zoom;
    float targetY = c.centerY;
    if (c.zoom < 1.0f) {
        // Half the height the view covers at the target. Keep the bottom edge where the full
        // framing has it, but no higher than the middle of the juggler, and never above the
        // full framing's center (so zooming in from a wide view is continuous).
        const float halfHeight = d * tanHalf;
        const float jugglerMiddle = 0.5f * (c.bottomY + kJugglerTopY);
        targetY = std::min(std::max(c.bottomY + halfHeight, jugglerMiddle), c.centerY);
    }
    v.target = Vec3(c.centerX, targetY, c.centerZ);
    float pitch = std::clamp(c.pitch + c.basePitch, -1.45f, 1.45f);
    float distance = d;
    if (c.topBlend > 0.001f) {
        // Looking down on the floor (whatever the user's pitch), the whole floor in view.
        const float b = c.topBlend;
        const float topDistance = c.zoom * 1.1f * c.floorHalf / tanHalf * std::max(1.0f, 1.0f / aspect);
        v.target = Vec3(c.centerX * (1.0f - b), targetY * (1.0f - b), c.centerZ * (1.0f - b));
        pitch = pitch + (1.45f - pitch) * b;
        distance = d + (topDistance - d) * b;
    }
    const Vec3 dir(std::sin(c.yaw) * std::cos(pitch), std::sin(pitch), std::cos(c.yaw) * std::cos(pitch));
    v.position = v.target + dir * distance;
    v.farPlane = distance * 3.0f + 50.0f;
    if (c.free) {
        // Exactly where the user put it. (The automatic framing above keeps up meanwhile, so
        // re-framing starts from today's pattern.)
        v.position = c.freePos;
        v.target = c.freePos - cameraBackDir(c.freeYaw, c.freePitch);
        v.farPlane = 150.0f;
    }
    return v;
}

Mat4 cameraViewProj(const CameraView& camera, float aspect) {
    const Mat4 proj = Mat4::perspective(kCameraFovY, aspect, 0.05f, camera.farPlane);
    const Mat4 view = Mat4::lookAt(camera.position, camera.target, {0.0f, 1.0f, 0.0f});
    return proj * view;
}

// Where a world point lands in a screen rectangle (ImGui pixels, origin top-left), and how
// many pixels a meter covers there. False if the point is behind the camera.
bool projectToScreen(const Mat4& viewProj, Vec3 p, ImVec2 rectMin, ImVec2 rectSize, ImVec2* screen,
                     float* pixelsPerMeter = nullptr) {
    const float x = viewProj.at(0, 0) * p.x + viewProj.at(0, 1) * p.y + viewProj.at(0, 2) * p.z + viewProj.at(0, 3);
    const float y = viewProj.at(1, 0) * p.x + viewProj.at(1, 1) * p.y + viewProj.at(1, 2) * p.z + viewProj.at(1, 3);
    const float w = viewProj.at(3, 0) * p.x + viewProj.at(3, 1) * p.y + viewProj.at(3, 2) * p.z + viewProj.at(3, 3);
    if (w < 0.05f) return false;
    screen->x = rectMin.x + (x / w * 0.5f + 0.5f) * rectSize.x;
    screen->y = rectMin.y + (0.5f - y / w * 0.5f) * rectSize.y;
    if (pixelsPerMeter) *pixelsPerMeter = 0.5f * rectSize.y / (std::tan(kCameraFovY * 0.5f) * w);
    return true;
}

// The point on the floor (y = 0) under the mouse in a view of the scene (as rendered with
// cameraViewProj(view, aspect) into rect). False if the mouse is above the horizon.
bool floorUnderMouse(const CameraView& view, float aspect, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse, Vec3* out) {
    if (rectSize.x <= 0.0f || rectSize.y <= 0.0f) return false;
    const Vec3 forward = normalize(view.target - view.position);
    const Vec3 right = normalize(cross(forward, Vec3(0.0f, 1.0f, 0.0f)));
    const Vec3 up = cross(right, forward);
    const float tanHalf = std::tan(kCameraFovY * 0.5f);
    const float nx = (mouse.x - rectMin.x) / rectSize.x * 2.0f - 1.0f;
    const float ny = 1.0f - (mouse.y - rectMin.y) / rectSize.y * 2.0f;
    const Vec3 dir = forward + right * (nx * tanHalf * aspect) + up * (ny * tanHalf);
    if (dir.y > -1e-4f) return false;
    const float s = -view.position.y / dir.y;
    *out = view.position + dir * s;
    out->y = 0.0f;
    return true;
}

// ---- Spike marks (choreography) in the 3D view ---------------------------------------------

constexpr float kMarkRadius = 0.22f;  // m: the mark's ring on the floor
constexpr float kMarkArrow = 0.5f;    // m: from its middle to the tip of its facing arrow

// A spike mark's middle and arrow tip in the world.
void markGeometry(const SpikeMark& m, float scale, Vec3* middle, Vec3* tip) {
    *middle = Vec3(m.x * scale, 0.0f, m.z * scale);
    *tip = *middle + Vec3(std::sin(m.yaw), 0.0f, std::cos(m.yaw)) * kMarkArrow;
}

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b);  // (below)

// The mark under the mouse (and whether it's its arrow, for turning it), or -1. The arrow is
// anywhere along it outside the ring, or near its tip (a dozen pixels either side); otherwise the
// mark is anywhere inside its ring, on the floor (`floor`: the floor under the mouse, if
// `onFloor`), or within a few pixels of it. Arrows come first, then the mark nearest the mouse.
int pickSpikeMark(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize,
                  ImVec2 mouse, bool onFloor, Vec3 floor, bool* onArrow) {
    constexpr float kArrowReach = 12.0f;  // pixels
    int bestArrow = -1, bestMark = -1;
    float bestArrowDistance = 1e9f, bestMarkDistance = 1e9f;
    *onArrow = false;
    for (size_t i = 0; i < c.marks.size(); ++i) {
        const SpikeMark& mark = c.marks[i];
        Vec3 middle, tip;
        markGeometry(mark, scale, &middle, &tip);
        const Vec3 along(std::sin(mark.yaw), 0.0f, std::cos(mark.yaw));
        ImVec2 m, t, e;
        float ppm = 0.0f;
        if (!projectToScreen(viewProj, middle, rectMin, rectSize, &m, &ppm)) continue;
        // The arrow: from the ring out to its tip.
        if (projectToScreen(viewProj, tip, rectMin, rectSize, &t) &&
            projectToScreen(viewProj, middle + along * (kMarkRadius * scale), rectMin, rectSize, &e)) {
            const float d = std::min(distanceToSegment(mouse, e, t), std::hypot(mouse.x - t.x, mouse.y - t.y));
            if (d <= kArrowReach && d < bestArrowDistance) {
                bestArrow = static_cast<int>(i);
                bestArrowDistance = d;
            }
        }
        // Inside the ring (on the floor; or, with no floor under the mouse, near its middle on screen).
        float inside = 0.0f;  // how far from the middle, in pixels
        bool in = false;
        // (At least kMinReach pixels round the middle, so a small, far-off mark is still easy to hit.)
        constexpr float kMinReach = 16.0f;
        const float ringPixels = kMarkRadius * scale * ppm;
        const float onScreen = std::hypot(mouse.x - m.x, mouse.y - m.y);
        if (onFloor) {
            const float d = std::hypot(floor.x - middle.x, floor.z - middle.z);
            inside = d * ppm;
            in = inside <= ringPixels + 4.0f || onScreen <= kMinReach;
        } else {
            inside = onScreen;
            in = inside <= std::max(ringPixels, kMinReach);
        }
        if (in && inside < bestMarkDistance) {
            bestMark = static_cast<int>(i);
            bestMarkDistance = inside;
        }
    }
    if (bestArrow >= 0) {
        *onArrow = true;
        return bestArrow;
    }
    return bestMark;
}

// What a walk link says, for the ladder and tooltips: "walks like J1 +24, 1/4 CCW".
std::string walkLinkLabel(const WalkLink& l) {
    if (!l.active()) return std::string();
    std::string s = "walks like J" + std::to_string(l.leader + 1) + " " + (l.offset >= 0 ? "+" : "") + std::to_string(l.offset);
    if (!l.turn.none()) {
        const std::string turn = turnText(l.turn);  // ("1/4 CCW (90°)": without the degrees)
        s += ", " + turn.substr(0, turn.find(" ("));
    }
    return s;
}

// Choreography paths: where each juggler with keyframes walks over the cycle, drawn on the
// floor in choreography mode. A point for each whole beat (beats a juggler stands still for
// share one point), with the way they face then.
struct PathPoint {
    int juggler = 0;
    int firstBeat = 0, beats = 1;  // the beats (in the cycle, from 0) it's for: firstBeat and
                                   // the beats-1 after it (round the cycle)
    Vec3 position;                 // where it's drawn (a little to one side, while walking)
    float yaw = 0.0f;              // the way the juggler faces
};
struct ChoreographyPaths {
    std::vector<PathPoint> points;
    std::vector<std::vector<Vec3>> lines;  // per juggler (empty: no keyframes), closed round the cycle
    std::vector<Vec3> labelSpots;          // a "Jn" label in the middle of each path's longest walk...
    std::vector<int> labelJugglers;        // ...for this juggler
};

// While walking, a path is drawn a few centimeters to the walker's right, so two jugglers
// walking the same line opposite ways show as two lines. (In proportion to their speed, so it
// joins up smoothly where they stop.)
// (`keys`: the juggler's keyframes over the whole loop, `full` beats; see effectiveKeyframes.)
Vec3 pathSpot(const Choreography& c, const std::vector<Keyframe>& keys, int full, double beat, float scale, float* yaw) {
    Vec3 p, a, b;
    float y = 0.0f, unused = 0.0f;
    placementFromKeyframes(c, keys, full, beat, scale, &p, &y);
    placementFromKeyframes(c, keys, full, beat - 0.02, scale, &a, &unused);
    placementFromKeyframes(c, keys, full, beat + 0.02, scale, &b, &unused);
    if (yaw) *yaw = y;
    const Vec3 d = b - a;
    const float moved = length(d);
    if (moved < 1e-6f) return p;
    const float speed = std::min(1.0f, moved / (0.04f * 0.3f));  // full offset from 0.3 m a beat
    const Vec3 right = Vec3(-d.z, 0.0f, d.x) * (1.0f / moved);
    return p + right * (0.04f * speed);
}

// `written`: the beats written out; the paths run over the whole choreography (its walking loop
// and all).
ChoreographyPaths choreographyPaths(const Choreography& c, int jugglers, int written, float scale) {
    ChoreographyPaths paths;
    paths.lines.resize(static_cast<size_t>(std::max(0, jugglers)));
    if (written <= 0) return paths;
    const int cycle = choreographyFullCycle(c, written);
    for (int j = 0; j < jugglers; ++j) {
        // (Worked out once: a juggler's keyframes over the whole loop.)
        const std::vector<Keyframe> keys = effectiveKeyframes(c, j, written);
        if (keys.empty() || (!c.walks(j) && c.repeats() == 1)) continue;
        constexpr int kSamplesPerBeat = 12;
        std::vector<Vec3>& line = paths.lines[static_cast<size_t>(j)];
        for (int i = 0; i <= cycle * kSamplesPerBeat; ++i)
            line.push_back(pathSpot(c, keys, cycle, static_cast<double>(i) / kSamplesPerBeat, scale, nullptr));
        // Each beat's spot, unmoved, to find the stand-stills.
        std::vector<Vec3> at(static_cast<size_t>(cycle));
        std::vector<float> facing(static_cast<size_t>(cycle));
        for (int b = 0; b < cycle; ++b)
            placementFromKeyframes(c, keys, cycle, b, scale, &at[static_cast<size_t>(b)], &facing[static_cast<size_t>(b)]);
        auto same = [&](int b0, int b1) {
            return length(at[static_cast<size_t>(b0)] - at[static_cast<size_t>(b1)]) < 0.001f &&
                   std::fabs(std::remainder(facing[static_cast<size_t>(b0)] - facing[static_cast<size_t>(b1)], 2.0f * kPi)) < 0.01f;
        };
        // Start where something changes, so a stand-still across the cycle's end is one point.
        int start = 0;
        while (start < cycle && same(start, (start + cycle - 1) % cycle)) ++start;
        if (start == cycle) {  // the same spot all cycle
            PathPoint pt;
            pt.juggler = j;
            pt.firstBeat = 0;
            pt.beats = cycle;
            pt.position = at[0];
            pt.yaw = facing[0];
            paths.points.push_back(pt);
            continue;
        }
        const size_t firstOfJuggler = paths.points.size();
        for (int i = 0; i < cycle; ++i) {
            const int b = (start + i) % cycle;
            if (i > 0 && same(b, (b + cycle - 1) % cycle)) {
                ++paths.points.back().beats;
                continue;
            }
            PathPoint pt;
            pt.juggler = j;
            pt.firstBeat = b;
            pt.position = pathSpot(c, keys, cycle, b, scale, &pt.yaw);
            paths.points.push_back(pt);
        }
        // The juggler's number on their path: in the middle of their longest walk.
        const size_t count = paths.points.size() - firstOfJuggler;
        double bestMiddle = -1.0;
        int bestLength = 0;
        for (size_t i = 0; i < count; ++i) {
            const PathPoint& from = paths.points[firstOfJuggler + i];
            const PathPoint& to = paths.points[firstOfJuggler + (i + 1) % count];
            const int leave = from.firstBeat + from.beats - 1;
            int arrive = to.firstBeat;
            if (arrive <= leave) arrive += cycle;
            if (arrive - leave > bestLength) {
                bestLength = arrive - leave;
                bestMiddle = 0.5 * (leave + arrive);
            }
        }
        if (bestMiddle >= 0.0) {
            paths.labelSpots.push_back(pathSpot(c, keys, cycle, bestMiddle, scale, nullptr));
            paths.labelJugglers.push_back(j);
        }
    }
    return paths;
}

// The beats a path point is for, as shown: "5", or "5-8" for a stand-still.
std::string pathPointBeats(const PathPoint& p, int cycle) {
    if (p.beats <= 1) return std::to_string(p.firstBeat + 1);
    if (p.beats >= cycle) return "1-" + std::to_string(cycle);
    return std::to_string(p.firstBeat + 1) + "-" + std::to_string((p.firstBeat + p.beats - 1) % cycle + 1);
}

// ---- Jugglers in the 3D view --------------------------------------------------------------

Mat4 jugglerMatrix(const JugglerState& juggler) {
    return Mat4::translation(juggler.position) * Mat4::rotationY(juggler.yaw);
}

JugglerPose posedJuggler(const JugglerState& juggler, PropType prop) {
    JugglerPose pose = makeNeutralPose();
    poseBody(pose, juggler.body);
    poseArmsForPalms(pose, juggler.palmRight, juggler.palmLeft, juggler.body.intensity, prop != PropType::Ball);
    return pose;
}

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float abx = b.x - a.x, aby = b.y - a.y;
    const float lengthSq = abx * abx + aby * aby;
    float t = lengthSq > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSq : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = a.x + abx * t - p.x, dy = a.y + aby * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
}

// The juggler under the mouse (the body parts as seen on screen, a little generously), or -1.
// The juggler under the mouse, or -1. `slack`: pixels added round each part (generous for
// picking a juggler; none for telling their body from a spike mark under their feet).
int pickJuggler(const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse,
                float slack = 6.0f) {
    int best = -1;
    float bestMiss = 0.0f;  // how far inside the part's outline the mouse is, relative (smaller is better)
    for (size_t j = 0; j < scene.jugglers.size(); ++j) {
        const JugglerPose pose = posedJuggler(scene.jugglers[j], scene.prop);
        const Mat4 placement = jugglerMatrix(scene.jugglers[j]);
        struct Part { Vec3 a, b; float radius; };
        const Part parts[] = {
            {pose.head, pose.head, pose.headRadius},
            {pose.neckBase, pose.waist, 0.13f},
            {pose.shoulderR, pose.shoulderL, 0.06f},
            {pose.shoulderR, pose.elbowR, 0.05f}, {pose.elbowR, pose.wristR, 0.05f},
            {pose.shoulderL, pose.elbowL, 0.05f}, {pose.elbowL, pose.wristL, 0.05f},
            {pose.hipR, pose.hipL, 0.07f},
            {pose.hipR, pose.kneeR, 0.06f}, {pose.kneeR, pose.ankleR, 0.05f},
            {pose.hipL, pose.kneeL, 0.06f}, {pose.kneeL, pose.ankleL, 0.05f},
        };
        for (const Part& part : parts) {
            ImVec2 a, b;
            float ppm = 0.0f;
            if (!projectToScreen(viewProj, transformPoint(placement, part.a), rectMin, rectSize, &a, &ppm)) continue;
            if (!projectToScreen(viewProj, transformPoint(placement, part.b), rectMin, rectSize, &b)) continue;
            const float reach = std::max(part.radius * ppm, 0.0f) + slack;
            if (reach <= 0.0f) continue;
            const float miss = distanceToSegment(mouse, a, b) / reach;
            if (miss <= 1.0f && (best < 0 || miss < bestMiss)) {
                best = static_cast<int>(j);
                bestMiss = miss;
            }
        }
    }
    return best;
}

// Juggler numbers ("J1", "J2", as on the ladder) over their heads (only drawn with two or more
// jugglers). The selected juggler's number is inverted (dark on light), so selection doesn't
// depend on color.
// Juggler number labels ("J1") over their heads. Labels that would overlap are pushed apart
// sideways, so two jugglers in the same place both show; a label pushed well away from its
// juggler gets a thin line down to them. *lastCenters (the labels' screen x last frame) keeps
// the labels in the same order while they're pushed apart, so they don't swap places as the
// jugglers pass each other.
void drawJugglerLabels(ImDrawList* dl, const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin,
                       ImVec2 rectSize, int selected, std::vector<float>* lastCenters) {
    if (scene.jugglers.size() < 2) {
        lastCenters->clear();
        return;
    }
    const float em = ImGui::GetFontSize();
    const float padX = em * 0.45f, padY = em * 0.15f;
    const float gap = em * 0.25f;
    struct Label {
        bool shown = false;
        ImVec2 anchor;     // over the head
        float center = 0;  // where the box goes (x)
        float width = 0, height = 0;
        char text[16] = {};
    };
    const size_t count = scene.jugglers.size();
    std::vector<Label> labels(count);
    for (size_t j = 0; j < count; ++j) {
        Label& l = labels[j];
        const JugglerPose pose = posedJuggler(scene.jugglers[j], scene.prop);
        const Vec3 above = transformPoint(jugglerMatrix(scene.jugglers[j]), pose.head) +
                           Vec3(0.0f, pose.headRadius + 0.08f, 0.0f);
        if (!projectToScreen(viewProj, above, rectMin, rectSize, &l.anchor)) continue;
        l.shown = true;
        std::snprintf(l.text, sizeof(l.text), "J%d", static_cast<int>(j) + 1);
        const ImVec2 textSize = ImGui::CalcTextSize(l.text);
        l.width = std::max(textSize.x + 2.0f * padX, textSize.y + 2.0f * padY);
        l.height = textSize.y + 2.0f * padY;
        l.center = l.anchor.x;
    }
    const bool haveLast = lastCenters->size() == count;
    // Push overlapping pairs apart, a few rounds (enough for a handful of jugglers in a heap).
    for (int round = 0; round < 12; ++round) {
        bool moved = false;
        for (size_t i = 0; i < count; ++i)
            for (size_t k = i + 1; k < count; ++k) {
                Label& a = labels[i];
                Label& b = labels[k];
                if (!a.shown || !b.shown) continue;
                if (std::fabs(a.anchor.y - b.anchor.y) >= 0.5f * (a.height + b.height)) continue;  // not level
                const float need = 0.5f * (a.width + b.width) + gap;
                const float dx = b.center - a.center;
                if (std::fabs(dx) >= need - 0.01f) continue;
                // Which goes left: as last frame, else as their jugglers are, else J1 first.
                float side = 0.0f;
                if (haveLast) side = (*lastCenters)[k] - (*lastCenters)[i];
                if (std::fabs(side) < 0.5f) side = b.anchor.x - a.anchor.x;
                if (std::fabs(side) < 0.5f) side = 1.0f;
                const float sign = side > 0.0f ? 1.0f : -1.0f;
                const float shift = 0.5f * (need - sign * dx);
                a.center -= sign * shift;
                b.center += sign * shift;
                moved = true;
            }
        if (!moved) break;
    }
    lastCenters->assign(count, 0.0f);
    for (size_t j = 0; j < count; ++j) (*lastCenters)[j] = labels[j].center;

    dl->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);
    for (size_t j = 0; j < count; ++j) {
        const Label& l = labels[j];
        if (!l.shown) continue;
        const ImVec2 boxMin(l.center - l.width * 0.5f, l.anchor.y - l.height);
        const ImVec2 boxMax(l.center + l.width * 0.5f, l.anchor.y);
        if (std::fabs(l.center - l.anchor.x) > l.width * 0.5f)
            dl->AddLine(ImVec2(l.center, boxMax.y), ImVec2(l.anchor.x, l.anchor.y + em * 0.4f), IM_COL32(210, 214, 224, 170), 1.0f);
        const bool isSelected = static_cast<int>(j) == selected;
        const float rounding = em * 0.3f;
        dl->AddRectFilled(boxMin, boxMax, isSelected ? IM_COL32(236, 238, 244, 255) : IM_COL32(28, 30, 36, 210),
                          rounding);
        dl->AddRect(boxMin, boxMax, IM_COL32(210, 214, 224, 230), rounding, 0, isSelected ? 2.0f : 1.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(l.text);
        dl->AddText(ImVec2(l.center - textSize.x * 0.5f, boxMin.y + padY),
                    isSelected ? IM_COL32(16, 16, 20, 255) : IM_COL32(232, 234, 240, 255), l.text);
    }
    dl->PopClipRect();
}

// ---- Floor overlays: spike marks, choreography paths, the orbit reticle ---------------------
//
// Their lines are drawn in the 3D pass, depth-tested, so the jugglers' bodies hide them; their
// text is drawn by ImGui, and left out where a juggler's body is in front of it.

struct Rgba {
    float r, g, b, a;
};
Rgba rgb255(int r, int g, int b) { return Rgba{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}; }

// The colour each group of spike marks is drawn in (with its dash pattern and number too, so it
// doesn't rest on colour alone).
Rgba markGroupColor(int g) {
    static const Rgba kColors[Choreography::kMarkGroups] = {
        rgb255(86, 150, 230),  rgb255(242, 142, 43),  rgb255(225, 87, 89),   rgb255(118, 200, 190), rgb255(110, 190, 90),
        rgb255(200, 120, 220), rgb255(255, 157, 167), rgb255(190, 150, 110), rgb255(160, 200, 255), rgb255(210, 210, 210)};
    return kColors[((g % Choreography::kMarkGroups) + Choreography::kMarkGroups) % Choreography::kMarkGroups];
}
// A group's key: 1-9, and 0 for the tenth.
char markGroupKey(int g) { return g == 9 ? '0' : static_cast<char>('1' + g); }

const Rgba kOverlayEdge{0.08f, 0.08f, 0.09f, 0.85f};  // the dark edge round light lines
const Rgba kMarkColor = rgb255(236, 226, 150);        // spike marks: pale yellow, solid
const Rgba kPathSelected = rgb255(90, 210, 255);      // the selected juggler's path: bright cyan, dashed
const Rgba kPathOther = rgb255(105, 135, 165);        // everyone else's: dim blue-gray, dashed
const Rgba kPathHot = rgb255(255, 255, 255);          // the point under the mouse
const Rgba kReticleActive = rgb255(244, 244, 248);    // the orbit reticle: white...
const Rgba kReticleFaint = rgb255(150, 150, 158);     // ...or gray when it's only showing where
constexpr float kOverlayLift = 0.01f;                 // m above the floor

ImU32 toImU32(Rgba c) {
    return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f),
                    static_cast<int>(c.a * 255.0f));
}

// Builds overlay lines and dots as triangles for Renderer::drawOverlay. Widths are in screen
// points, so lines stay as thick near and far.
class OverlayBuilder {
public:
    OverlayBuilder(std::vector<GlowVertex>* out, const CameraView& view, float viewHeight)
        : out_(out), eye_(view.position), viewHeight_(std::max(1.0f, viewHeight)) {
        const Vec3 forward = normalize(view.target - view.position);
        right_ = normalize(cross(forward, Vec3(0.0f, 1.0f, 0.0f)));
        up_ = cross(right_, forward);
    }
    // Meters per screen point at p.
    float pointSize(Vec3 p) const { return 2.0f * length(p - eye_) * std::tan(kCameraFovY * 0.5f) / viewHeight_; }

    void line(Vec3 a, Vec3 b, Rgba c, float width) {
        const Vec3 d = b - a;
        const float len = length(d);
        if (len < 1e-6f) return;
        const Vec3 along = d * (1.0f / len);
        Vec3 side = cross(d, eye_ - (a + b) * 0.5f);
        const float sideLen = length(side);
        if (sideLen < 1e-9f) return;
        side = side * (1.0f / sideLen);
        // Half a width further at each end, so the segments of a polyline join up.
        const float ha = 0.5f * width * pointSize(a), hb = 0.5f * width * pointSize(b);
        const Vec3 a0 = a - along * ha, b0 = b + along * hb;
        quad(a0 + side * ha, a0 - side * ha, b0 - side * hb, b0 + side * hb, c);
    }
    // Along points; with dashOn > 0, dashed (dashOn meters drawn, dashOff skipped).
    void polyline(const std::vector<Vec3>& points, Rgba c, float width, float dashOn = 0.0f, float dashOff = 0.0f) {
        float phase = 0.0f;  // distance into the current dash period
        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const Vec3 a = points[i], b = points[i + 1];
            const float len = length(b - a);
            if (dashOn <= 0.0f) {
                line(a, b, c, width);
                continue;
            }
            float t = 0.0f;
            while (t < len) {
                const float period = dashOn + dashOff;
                const bool on = phase < dashOn;
                const float step = std::min(len - t, (on ? dashOn : period) - phase);
                if (on) line(a + (b - a) * (t / len), a + (b - a) * ((t + step) / len), c, width);
                t += step;
                phase += step;
                if (phase >= period - 1e-6f) phase = 0.0f;
            }
        }
    }
    // A round dot facing the camera, `radius` points across.
    void disc(Vec3 p, float radius, Rgba c) {
        const float r = radius * pointSize(p);
        constexpr int kSides = 12;
        for (int i = 0; i < kSides; ++i) {
            const float a0 = 2.0f * kPi * i / kSides, a1 = 2.0f * kPi * (i + 1) / kSides;
            vertex(p, c);
            vertex(p + right_ * (std::cos(a0) * r) + up_ * (std::sin(a0) * r), c);
            vertex(p + right_ * (std::cos(a1) * r) + up_ * (std::sin(a1) * r), c);
        }
    }

private:
    void vertex(Vec3 p, Rgba c) { out_->push_back(GlowVertex{p.x, p.y, p.z, c.r, c.g, c.b, c.a}); }
    void quad(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Rgba c) {
        vertex(p0, c);
        vertex(p1, c);
        vertex(p2, c);
        vertex(p0, c);
        vertex(p2, c);
        vertex(p3, c);
    }
    std::vector<GlowVertex>* out_;
    Vec3 eye_, right_, up_;
    float viewHeight_;
};

// Whether a juggler's body is between the eye and a point: the head, torso, arms and legs, as
// spheres and capsules a little fatter than drawn.
float segmentDistance(Vec3 p0, Vec3 p1, Vec3 q0, Vec3 q1) {
    const Vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    float s = 0.0f, t = 0.0f;
    if (a <= 1e-9f && e <= 1e-9f) return length(r);
    if (a <= 1e-9f) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = dot(d1, r);
        if (e <= 1e-9f) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = dot(d1, d2), denom = a * e - b * b;
            s = denom > 1e-9f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return length((p0 + d1 * s) - (q0 + d2 * t));
}

struct BodyOccluder {
    struct Capsule {
        Vec3 a, b;
        float radius;
    };
    std::vector<Capsule> capsules;
    Vec3 eye;

    BodyOccluder(const JugglerScene& scene, Vec3 eyePosition) : eye(eyePosition) {
        for (const JugglerState& js : scene.jugglers) {
            const JugglerPose pose = posedJuggler(js, scene.prop);
            const Mat4 m = jugglerMatrix(js);
            auto add = [&](Vec3 a, Vec3 b, float radius) {
                capsules.push_back(Capsule{transformPoint(m, a), transformPoint(m, b), radius});
            };
            add(pose.head, pose.head, pose.headRadius);
            add(pose.waist, pose.neckBase, 0.14f);
            add(pose.shoulderR, pose.elbowR, 0.04f);
            add(pose.elbowR, pose.wristR, 0.04f);
            add(pose.shoulderL, pose.elbowL, 0.04f);
            add(pose.elbowL, pose.wristL, 0.04f);
            add(pose.hipR, pose.kneeR, 0.05f);
            add(pose.kneeR, pose.ankleR, 0.05f);
            add(pose.hipL, pose.kneeL, 0.05f);
            add(pose.kneeL, pose.ankleL, 0.05f);
        }
    }
    bool hides(Vec3 p) const {
        // Stop just short of the point, so a body it's touching (a mark under someone's feet)
        // doesn't hide it from above.
        const Vec3 d = p - eye;
        const float len = length(d);
        if (len < 0.05f) return false;
        const Vec3 end = eye + d * ((len - 0.05f) / len);
        for (const Capsule& c : capsules)
            if (segmentDistance(eye, end, c.a, c.b) < c.radius) return true;
        return false;
    }
};

// Text with a dark edge, centered on (x) / beside a point, unless a body hides the point.
void overlayText(ImDrawList* dl, ImVec2 at, const char* text, Rgba color) {
    dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), toImU32(kOverlayEdge), text);
    dl->AddText(at, toImU32(color), text);
}

// Spike marks: a ring on the floor and an arrow the way it faces (lines), and its number (text).
// `hot` is drawn bolder.
void buildSpikeMarks(OverlayBuilder& overlay, const Choreography& c, float scale, int hot,
                     const std::vector<int>& highlighted) {
    for (size_t i = 0; i < c.marks.size(); ++i) {
        Vec3 middle, tip;
        markGeometry(c.marks[i], scale, &middle, &tip);
        const Vec3 lift(0.0f, kOverlayLift, 0.0f);
        middle = middle + lift;
        tip = tip + lift;
        // Highlighted (to pick one of several marks in the same place): the arrow white and bold.
        const bool lit = std::find(highlighted.begin(), highlighted.end(), static_cast<int>(i)) != highlighted.end();
        const float width = lit ? 4.0f : (static_cast<int>(i) == hot ? 3.0f : 2.0f);
        std::vector<Vec3> ring;
        for (int k = 0; k <= 32; ++k) {
            const float a = 2.0f * kPi * static_cast<float>(k) / 32.0f;
            ring.push_back(middle + Vec3(std::sin(a), 0.0f, std::cos(a)) * kMarkRadius);
        }
        const Vec3 side = Vec3(std::cos(c.marks[i].yaw), 0.0f, -std::sin(c.marks[i].yaw)) * 0.09f;
        const Vec3 back = (middle - tip) * 0.22f;
        for (int pass = 0; pass < 2; ++pass) {
            const Rgba col = pass == 0 ? kOverlayEdge : kMarkColor;
            const Rgba arrowCol = pass == 0 ? kOverlayEdge : (lit ? rgb255(255, 255, 255) : kMarkColor);
            const float w = pass == 0 ? width + 2.0f : width;
            overlay.polyline(ring, col, pass == 0 ? 4.0f : 2.0f);
            overlay.line(middle, tip, arrowCol, w);
            overlay.line(tip, tip + back + side, arrowCol, w);
            overlay.line(tip, tip + back - side, arrowCol, w);
        }
    }
}

// Where each spike mark's number bubble goes on screen: on the mark's middle (so it's never
// nearer another mark than its own); where bubbles would overlap (marks in the same place, or
// close), each moves out along its own arrow just far enough to clear the others (apart along
// the line between them, if two arrows point the same way). shown[i] false: off screen.
// (`extraH`: room under each bubble for its group badges, while they're shown.)
std::vector<ImVec2> markBubbleCenters(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin,
                                      ImVec2 rectSize, std::vector<bool>* shown, float extraH = 0.0f) {
    const size_t n = c.marks.size();
    std::vector<ImVec2> at(n), along(n, ImVec2(1.0f, 0.0f));
    shown->assign(n, false);
    for (size_t i = 0; i < n; ++i) {
        Vec3 middle, tip;
        markGeometry(c.marks[i], scale, &middle, &tip);
        ImVec2 p, t;
        if (projectToScreen(viewProj, middle, rectMin, rectSize, &p)) {
            at[i] = p;
            (*shown)[i] = true;
            if (projectToScreen(viewProj, tip, rectMin, rectSize, &t)) {
                const float len = std::hypot(t.x - p.x, t.y - p.y);
                if (len > 0.5f) along[i] = ImVec2((t.x - p.x) / len, (t.y - p.y) / len);
            }
        }
    }
    const float em = ImGui::GetFontSize();
    const float w = em * 1.9f, h = em * 1.25f + extraH;  // about a bubble's size
    for (int round = 0; round < 40; ++round) {
        bool moved = false;
        for (size_t i = 0; i < n; ++i)
            for (size_t k = i + 1; k < n; ++k) {
                if (!(*shown)[i] || !(*shown)[k]) continue;
                const float dx = at[k].x - at[i].x, dy = at[k].y - at[i].y;
                if (std::fabs(dx) >= w || std::fabs(dy) >= h) continue;
                const float same = along[i].x * along[k].x + along[i].y * along[k].y;
                if (same > 0.8f) {
                    // Pointing the same way: apart along the line between them (sideways if
                    // they're on top of each other).
                    const float len = std::hypot(dx, dy);
                    const float ux = len > 0.5f ? dx / len : -along[i].y, uy = len > 0.5f ? dy / len : along[i].x;
                    const float need = 0.5f * (std::fabs(ux) * w + std::fabs(uy) * h) - 0.5f * len + 1.0f;
                    at[i] = ImVec2(at[i].x - ux * need, at[i].y - uy * need);
                    at[k] = ImVec2(at[k].x + ux * need, at[k].y + uy * need);
                } else {
                    // Each a little further out along its own arrow (small steps, so they stop
                    // as soon as they're clear).
                    const float step = em * 0.15f;
                    at[i] = ImVec2(at[i].x + along[i].x * step, at[i].y + along[i].y * step);
                    at[k] = ImVec2(at[k].x + along[k].x * step, at[k].y + along[k].y * step);
                }
                moved = true;
            }
        if (!moved) break;
    }
    return at;
}

// The spike mark whose name bubble is under `mouse` (the one drawn last, if they overlap), or -1.
int markLabelUnder(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize,
                   ImVec2 mouse, float extraH = 0.0f) {
    std::vector<bool> shown;
    const std::vector<ImVec2> at = markBubbleCenters(c, scale, viewProj, rectMin, rectSize, &shown, extraH);
    const float em = ImGui::GetFontSize();
    int found = -1;
    for (size_t i = 0; i < c.marks.size(); ++i) {
        if (!shown[i]) continue;
        const ImVec2 ts = ImGui::CalcTextSize(c.marks[i].name.c_str());
        const float halfW = std::max(ts.x * 0.5f + em * 0.35f, ts.y * 0.5f + em * 0.1f);
        const float halfH = ts.y * 0.5f + em * 0.1f;
        if (std::fabs(mouse.x - at[i].x) <= halfW && std::fabs(mouse.y - at[i].y) <= halfH) found = static_cast<int>(i);
    }
    return found;
}

// What's shown of the groups of spike marks: badges under every name (one column per group, up
// to the highest there is: "1 3 5"), and the groups whose marks are joined up by lines.
struct MarkGroupView {
    int slots = 0;                 // badge columns (0: no badges)
    std::vector<int> lines;        // the groups drawn
    float badgeH() const { return slots > 0 ? ImGui::GetTextLineHeight() + 2.0f : 0.0f; }
};

// A line from a to b, dashed in the group's pattern (solid, long dashes, short dashes, dots, dash
// and dot), with a dark edge.
void groupLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, int g) {
    static const float kPatterns[5][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {14.0f, 6.0f, 0.0f, 0.0f}, {6.0f, 5.0f, 0.0f, 0.0f},
                                          {2.0f, 5.0f, 0.0f, 0.0f}, {12.0f, 4.0f, 2.0f, 4.0f}};
    const float* pat = kPatterns[g % 5];
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (len < 1.0f) return;
    const ImVec2 u((b.x - a.x) / len, (b.y - a.y) / len);
    auto seg = [&](float t0, float t1) {
        const ImVec2 p(a.x + u.x * t0, a.y + u.y * t0), q(a.x + u.x * t1, a.y + u.y * t1);
        dl->AddLine(p, q, IM_COL32(10, 10, 14, 200), 5.0f);
        dl->AddLine(p, q, col, 2.5f);
    };
    if (pat[1] == 0.0f) {
        seg(0.0f, len);
        return;
    }
    float t = 0.0f;
    for (int k = 0; t < len; k = (k + 2) % 4) {
        if (pat[k] == 0.0f) k = 0;
        seg(t, std::min(len, t + pat[k]));
        t += pat[k] + pat[k + 1];
    }
}

// The spike marks' numbers, in bubbles on their middles (see markBubbleCenters). Behind a body,
// they're dimmed to half, still readable (unless highlighted: then as usual). With `groups`: the
// groups' lines first (each group's marks joined, mark to nearest mark, between their names, so
// marks on one spot are told apart), and the badges under the names.
void drawSpikeMarkNumbers(ImDrawList* dl, const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin,
                          ImVec2 rectSize, const BodyOccluder& bodies, const std::vector<int>& highlighted,
                          const MarkGroupView& groups) {
    dl->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);
    std::vector<bool> shown;
    const std::vector<ImVec2> at = markBubbleCenters(c, scale, viewProj, rectMin, rectSize, &shown, groups.badgeH());
    const float em = ImGui::GetFontSize();
    // The groups' lines: each group's marks joined by the shortest lines that connect them all,
    // with the group's key in a disc on its longest line (or beside a lone mark).
    for (const int g : groups.lines) {
        std::vector<int> members;
        for (const int m : c.group(g))
            if (m >= 0 && m < static_cast<int>(c.marks.size()) && shown[static_cast<size_t>(m)]) members.push_back(m);
        if (members.empty()) continue;
        const Rgba rgba = markGroupColor(g);
        const ImU32 col = toImU32(rgba);
        ImVec2 tagAt(at[static_cast<size_t>(members[0])].x + em * 1.6f, at[static_cast<size_t>(members[0])].y - em * 0.9f);
        float longest = -1.0f;
        std::vector<char> joined(members.size(), 0);
        joined[0] = 1;
        for (size_t added = 1; added < members.size(); ++added) {
            float best = 1e30f;
            size_t from = 0, to = 0;
            for (size_t i = 0; i < members.size(); ++i) {
                if (!joined[i]) continue;
                for (size_t k = 0; k < members.size(); ++k) {
                    if (joined[k]) continue;
                    const ImVec2 p = at[static_cast<size_t>(members[i])], q = at[static_cast<size_t>(members[k])];
                    const float d = std::hypot(q.x - p.x, q.y - p.y);
                    if (d < best) {
                        best = d;
                        from = i;
                        to = k;
                    }
                }
            }
            joined[to] = 1;
            const ImVec2 p = at[static_cast<size_t>(members[from])], q = at[static_cast<size_t>(members[to])];
            groupLine(dl, p, q, col, g);
            if (best > longest) {
                longest = best;
                tagAt = ImVec2((p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f);
            }
        }
        if (members.size() == 1) {
            const ImVec2 p = at[static_cast<size_t>(members[0])];
            dl->AddCircle(p, em * 1.25f, IM_COL32(10, 10, 14, 200), 32, 5.0f);
            dl->AddCircle(p, em * 1.25f, col, 32, 2.5f);
        }
        const char key[2] = {markGroupKey(g), 0};
        const ImVec2 ks = ImGui::CalcTextSize(key);
        const float r = em * 0.62f;
        dl->AddCircleFilled(tagAt, r, IM_COL32(16, 16, 20, 235));
        dl->AddCircle(tagAt, r, col, 0, 2.0f);
        dl->AddText(ImVec2(tagAt.x - ks.x * 0.5f, tagAt.y - ks.y * 0.5f), col, key);
    }
    for (size_t i = 0; i < c.marks.size(); ++i) {
        if (!shown[i]) continue;
        const bool lit = std::find(highlighted.begin(), highlighted.end(), static_cast<int>(i)) != highlighted.end();
        Vec3 middle, tip;
        markGeometry(c.marks[i], scale, &middle, &tip);
        const bool behind = !lit && bodies.hides(middle + Vec3(0.0f, kOverlayLift, 0.0f));
        auto faded = [&](ImU32 col) {
            if (!behind) return col;
            const ImU32 alpha = ((col >> IM_COL32_A_SHIFT) & 0xFF) / 2;
            return (col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
        };
        const char* label = c.marks[i].name.c_str();
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const float padX = em * 0.35f, padY = em * 0.1f;
        const float halfW = std::max(ts.x * 0.5f + padX, ts.y * 0.5f + padY);
        const ImVec2 a(at[i].x - halfW, at[i].y - ts.y * 0.5f - padY), b(at[i].x + halfW, at[i].y + ts.y * 0.5f + padY);
        const ImU32 ink = lit ? IM_COL32(16, 16, 20, 255) : toImU32(kMarkColor);
        dl->AddRectFilled(a, b, faded(lit ? IM_COL32(255, 255, 255, 255) : IM_COL32(28, 30, 36, 215)), em * 0.3f);
        dl->AddRect(a, b, faded(toImU32(kMarkColor)), em * 0.3f, 0, lit ? 2.0f : 1.0f);
        dl->AddText(ImVec2(at[i].x - ts.x * 0.5f, at[i].y - ts.y * 0.5f), faded(ink), label);
        // Its groups: one column per group, in that group's colour ("1 3 5"; blank where it isn't in one).
        if (groups.slots > 0) {
            const float cw = ImGui::CalcTextSize("0").x;
            const float x0 = at[i].x - cw * static_cast<float>(groups.slots) * 0.5f, y0 = b.y + 1.0f;
            bool any = false;
            for (int g = 0; g < groups.slots; ++g) {
                const std::vector<int>& members = c.group(g);
                if (std::find(members.begin(), members.end(), static_cast<int>(i)) == members.end()) continue;
                if (!any)
                    dl->AddRectFilled(ImVec2(x0 - 2.0f, y0), ImVec2(x0 + cw * static_cast<float>(groups.slots) + 2.0f, y0 + ts.y),
                                      faded(IM_COL32(16, 16, 20, 200)), 2.0f);
                any = true;
                const char key[2] = {markGroupKey(g), 0};
                dl->AddText(ImVec2(x0 + cw * static_cast<float>(g), y0), faded(toImU32(markGroupColor(g))), key);
            }
        }
    }
    dl->PopClipRect();
}

// The orbit reticle at `center`: a ring with a cross on the floor under it and, if the center
// is above the floor, a small three-axis cross there and a line down to the ring.
void buildOrbitReticle(OverlayBuilder& overlay, Vec3 center, bool active) {
    const Rgba color = active ? kReticleActive : kReticleFaint;
    const Vec3 floor(center.x, kOverlayLift, center.z);
    const float radius = 0.25f, arm = radius * 1.3f, tick = 0.08f;
    std::vector<Vec3> ring;
    for (int i = 0; i <= 32; ++i) {
        const float a = 2.0f * kPi * i / 32.0f;
        ring.push_back(floor + Vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius));
    }
    for (int pass = 0; pass < 2; ++pass) {
        const Rgba col = pass == 0 ? kOverlayEdge : color;
        const float w = pass == 0 ? 3.0f : 1.5f;
        overlay.polyline(ring, col, w);
        overlay.line(floor - Vec3(arm, 0.0f, 0.0f), floor + Vec3(arm, 0.0f, 0.0f), col, w);
        overlay.line(floor - Vec3(0.0f, 0.0f, arm), floor + Vec3(0.0f, 0.0f, arm), col, w);
        if (center.y > 0.02f) {
            overlay.line(center - Vec3(tick, 0.0f, 0.0f), center + Vec3(tick, 0.0f, 0.0f), col, w);
            overlay.line(center - Vec3(0.0f, tick, 0.0f), center + Vec3(0.0f, tick, 0.0f), col, w);
            overlay.line(center - Vec3(0.0f, 0.0f, tick), center + Vec3(0.0f, 0.0f, tick), col, w);
            overlay.line(center, floor, col, w);
        }
    }
}

// ---- The floor grid (choreography) ----

const Rgba kGridColor{0.62f, 0.64f, 0.70f, 0.38f};        // faint neutral gray, under everything else
const Rgba kGridMidColor{0.70f, 0.72f, 0.78f, 0.62f};     // every few lines
const Rgba kGridOriginColor{0.82f, 0.84f, 0.90f, 0.90f};  // through the middle of the floor

// The floor grid as a choreography keeps it (Choreography::grid): "<type>,<spacing>,<lines between
// mid-weight ones>,<spokes>,<spokes between mid-weight ones>,<half turn>" ("radial,0.5,4,8,0,1").
std::string floorGridText(const AppSettings& g) {
    static const char* const kTypes[] = {"none", "square", "radial", "triangles"};
    const int type = std::clamp(static_cast<int>(g.floorGrid), 0, 3);
    char text[96];
    std::snprintf(text, sizeof(text), "%s,%g,%d,%d,%d,%d", kTypes[type], static_cast<double>(g.gridSpacing),
                  g.gridMajorEvery, g.gridSpokes, g.gridSpokeMajor, g.gridHalfTurn ? 1 : 0);
    return text;
}
// ...and back into the settings. False (nothing changed) if it can't be read.
bool readFloorGridText(const std::string& text, AppSettings* g) {
    char type[16] = {0};
    double spacing = 0.0;
    int major = 0, spokes = 0, spokeMajor = 0, half = 0;
    if (std::sscanf(text.c_str(), "%15[a-z],%lf,%d,%d,%d,%d", type, &spacing, &major, &spokes, &spokeMajor, &half) != 6)
        return false;
    const std::string t = type;
    FloorGridType kind = FloorGridType::None;
    if (t == "square") kind = FloorGridType::Square;
    else if (t == "radial") kind = FloorGridType::Radial;
    else if (t == "triangles") kind = FloorGridType::Triangles;
    else if (t != "none") return false;
    g->floorGrid = kind;
    g->gridSpacing = static_cast<float>(std::clamp(spacing, 0.25, 2.0));
    g->gridMajorEvery = std::clamp(major, 2, 12);
    g->gridSpokes = std::clamp(spokes, 3, 32);
    g->gridSpokeMajor = std::max(0, spokeMajor);
    g->gridHalfTurn = half != 0;
    return true;
}

// Snaps a floor point (world meters) to the nearest point of the grid: a crossing of the square
// grid's lines, a crossing of the radial grid's rings and spokes (or its middle), a corner of the
// triangle grid. With no grid, to the nearest 5 cm.
void snapToFloorGrid(const AppSettings& g, float* x, float* z) {
    const float s = std::clamp(g.gridSpacing, 0.25f, 2.0f);
    switch (g.floorGrid) {
        case FloorGridType::None:
            *x = std::round(*x * 20.0f) / 20.0f;
            *z = std::round(*z * 20.0f) / 20.0f;
            return;
        case FloorGridType::Square:
            *x = std::round(*x / s) * s;
            *z = std::round(*z / s) * s;
            return;
        case FloorGridType::Radial: {
            const float r = std::round(std::hypot(*x, *z) / s) * s;
            if (r <= 0.0f) {
                *x = *z = 0.0f;
                return;
            }
            const float step = 2.0f * kPi / static_cast<float>(std::clamp(g.gridSpokes, 3, 32));
            const float turn = g.gridHalfTurn ? 0.5f * step : 0.0f;
            const float a = std::round((std::atan2(*x, *z) - turn) / step) * step + turn;
            *x = r * std::sin(a);
            *z = r * std::cos(a);
            return;
        }
        case FloorGridType::Triangles: {
            // Corners at i*(s, 0) + j*(s/2, h): rows along x, h apart.
            const float h = s * std::sqrt(3.0f) * 0.5f;
            const float jf = *z / h, i0 = std::floor((*x - std::floor(jf) * s * 0.5f) / s);
            float bestX = *x, bestZ = *z, bestD = 1e9f;
            for (int dj = -1; dj <= 2; ++dj)
                for (int di = -1; di <= 2; ++di) {
                    const float j = std::floor(jf) + static_cast<float>(dj);
                    const float i = i0 + static_cast<float>(di);
                    const float px = i * s + j * s * 0.5f, pz = j * h;
                    const float d = std::hypot(px - *x, pz - *z);
                    if (d < bestD) {
                        bestD = d;
                        bestX = px;
                        bestZ = pz;
                    }
                }
            *x = bestX;
            *z = bestZ;
            return;
        }
    }
}

// The radial grid's mid-weight spokes: every gridSpokeMajor-th spoke, if that divides the
// spokes evenly (else none: 0).
int effectiveSpokeMajor(const AppSettings& g) {
    const int spokes = std::clamp(g.gridSpokes, 3, 32);
    const int n = g.gridSpokeMajor;
    return n >= 2 && n < spokes && spokes % n == 0 ? n : 0;
}

// The grid's lines over the floor between (minX, minZ) and (maxX, maxZ), in three weights: fine
// lines, every gridMajorEvery-th line (or ring) mid-weight, and the lines through the middle of
// the floor heaviest, with a dot there.
void buildFloorGrid(OverlayBuilder& overlay, const AppSettings& g, float minX, float minZ, float maxX, float maxZ) {
    const float s = std::clamp(g.gridSpacing, 0.25f, 2.0f);
    const int major = std::clamp(g.gridMajorEvery, 2, 12);
    const Rgba colors[3] = {kGridColor, kGridMidColor, kGridOriginColor};
    const float widths[3] = {1.0f, 1.6f, 2.6f};
    // Which weight the line numbered k (0 through the middle) gets.
    auto weightOf = [&](long k) { return k == 0 ? 2 : (k % major == 0 ? 1 : 0); };
    for (int pass = 0; pass < 3; ++pass) {  // fine lines first, so heavier ones draw over them
        // A straight line, in short pieces (so its width stays even near and far).
        auto straight = [&](Vec3 a, Vec3 b) {
            const int pieces = std::max(1, static_cast<int>(std::ceil(length(b - a) / 0.5f)));
            std::vector<Vec3> points;
            for (int i = 0; i <= pieces; ++i) points.push_back(a + (b - a) * (static_cast<float>(i) / pieces));
            overlay.polyline(points, colors[pass], widths[pass]);
        };
        // The part of the line through p along d (unit) inside the area, if any.
        auto clipped = [&](Vec3 p, Vec3 d) {
            float t0 = -1e9f, t1 = 1e9f;
            const float origin[2] = {p.x, p.z}, dir[2] = {d.x, d.z}, lo[2] = {minX, minZ}, hi[2] = {maxX, maxZ};
            for (int k = 0; k < 2; ++k) {
                if (std::fabs(dir[k]) < 1e-6f) {
                    if (origin[k] < lo[k] || origin[k] > hi[k]) return;
                    continue;
                }
                float a = (lo[k] - origin[k]) / dir[k], b = (hi[k] - origin[k]) / dir[k];
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a);
                t1 = std::min(t1, b);
            }
            if (t1 > t0) straight(p + d * t0, p + d * t1);
        };
        const float lift = kOverlayLift;
        switch (g.floorGrid) {
            case FloorGridType::None:
                return;
            case FloorGridType::Square:
                for (long k = static_cast<long>(std::ceil(minX / s)); k * s <= maxX; ++k)
                    if (weightOf(k) == pass) clipped(Vec3(k * s, lift, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
                for (long k = static_cast<long>(std::ceil(minZ / s)); k * s <= maxZ; ++k)
                    if (weightOf(k) == pass) clipped(Vec3(0.0f, lift, k * s), Vec3(1.0f, 0.0f, 0.0f));
                break;
            case FloorGridType::Radial: {
                float reach = 0.0f;
                for (float x : {minX, maxX})
                    for (float z : {minZ, maxZ}) reach = std::max(reach, std::hypot(x, z));
                const long rings = static_cast<long>(std::floor(reach / s + 1e-4f));
                for (long k = 1; k <= rings; ++k) {
                    if (weightOf(k) != pass) continue;
                    const float r = k * s;
                    const int sides = std::max(48, static_cast<int>(r * 24.0f));
                    std::vector<Vec3> ring;
                    for (int i = 0; i <= sides; ++i) {
                        const float a = 2.0f * kPi * i / sides;
                        ring.push_back(Vec3(r * std::sin(a), lift, r * std::cos(a)));
                    }
                    overlay.polyline(ring, colors[pass], widths[pass]);
                }
                // Spokes: fine, or mid-weight every spokeMajor-th (the first pointing at the
                // default camera, or half a spoke round from it).
                const int spokes = std::clamp(g.gridSpokes, 3, 32);
                const int spokeMajor = effectiveSpokeMajor(g);
                const float step = 2.0f * kPi / static_cast<float>(spokes);
                const float outer = rings * s;
                for (int k = 0; k < spokes; ++k) {
                    const int weight = spokeMajor > 0 && k % spokeMajor == 0 ? 1 : 0;
                    if (weight != pass) continue;
                    const float a = (static_cast<float>(k) + (g.gridHalfTurn ? 0.5f : 0.0f)) * step;
                    straight(Vec3(0.0f, lift, 0.0f), Vec3(outer * std::sin(a), lift, outer * std::cos(a)));
                }
                break;
            }
            case FloorGridType::Triangles: {
                const float h = s * std::sqrt(3.0f) * 0.5f;
                const float c = 0.5f, r3 = std::sqrt(3.0f) * 0.5f;
                const Vec3 dirs[3] = {Vec3(1.0f, 0.0f, 0.0f), Vec3(c, 0.0f, r3), Vec3(-c, 0.0f, r3)};
                // Each family of parallel lines is h apart (along its normal), through the corners.
                const float reach =
                    std::max(std::max(std::fabs(minX), std::fabs(maxX)), std::max(std::fabs(minZ), std::fabs(maxZ))) * 1.5f;
                const long count = static_cast<long>(std::floor(reach / h));
                for (const Vec3& d : dirs) {
                    const Vec3 n(-d.z, 0.0f, d.x);
                    for (long k = -count; k <= count; ++k)
                        if (weightOf(k) == pass) clipped(Vec3(n.x * k * h, lift, n.z * k * h), d);
                }
                break;
            }
        }
    }
    // The middle of the floor.
    const Vec3 middle(0.0f, kOverlayLift, 0.0f);
    overlay.disc(middle, 5.5f, kOverlayEdge);
    overlay.disc(middle, 4.0f, kGridOriginColor);
}

// The path point under the mouse (and not behind a body), or -1.
int pickPathPoint(const ChoreographyPaths& paths, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse,
                  const BodyOccluder& bodies) {
    int best = -1;
    float bestD = 9.0f;  // pixels
    for (size_t i = 0; i < paths.points.size(); ++i) {
        ImVec2 at;
        if (!projectToScreen(viewProj, paths.points[i].position, rectMin, rectSize, &at)) continue;
        const float d = std::hypot(at.x - mouse.x, at.y - mouse.y);
        if (d < bestD && !bodies.hides(paths.points[i].position)) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Choreography paths: everyone's dim blue-gray, the selected juggler's bright cyan (drawn last,
// on top), dashed. At each point a dot and an arrow the way the juggler faces.
void buildChoreographyPaths(OverlayBuilder& overlay, const ChoreographyPaths& paths, int selected, int hovered) {
    const Vec3 lift(0.0f, kOverlayLift, 0.0f);
    for (int pass = 0; pass < 2; ++pass) {  // others, then the selected juggler
        auto mine = [&](int juggler) { return (juggler == selected) == (pass == 1); };
        const Rgba color = pass == 1 ? kPathSelected : kPathOther;
        const float width = pass == 1 ? 2.5f : 1.5f;
        for (size_t j = 0; j < paths.lines.size(); ++j) {
            if (!mine(static_cast<int>(j)) || paths.lines[j].empty()) continue;
            std::vector<Vec3> line = paths.lines[j];
            for (Vec3& p : line) p = p + lift;
            overlay.polyline(line, kOverlayEdge, width + 2.0f, 0.12f, 0.07f);
            overlay.polyline(line, color, width, 0.12f, 0.07f);
        }
        for (size_t i = 0; i < paths.points.size(); ++i) {
            const PathPoint& p = paths.points[i];
            if (!mine(p.juggler)) continue;
            const bool hot = static_cast<int>(i) == hovered;
            const Rgba c = hot ? kPathHot : color;
            const Vec3 at = p.position + lift;
            const Vec3 facing(std::sin(p.yaw), 0.0f, std::cos(p.yaw));
            const Vec3 side(std::cos(p.yaw), 0.0f, -std::sin(p.yaw));
            const Vec3 tip = at + facing * 0.16f;
            const float w = hot ? 2.5f : width * 0.8f;
            for (int k = 0; k < 2; ++k) {
                const Rgba col = k == 0 ? kOverlayEdge : c;
                const float lw = k == 0 ? w + 2.0f : w;
                overlay.line(at, tip, col, lw);
                overlay.line(tip, tip - facing * 0.05f + side * 0.035f, col, lw);
                overlay.line(tip, tip - facing * 0.05f - side * 0.035f, col, lw);
            }
            const float radius = hot ? 5.0f : (pass == 1 ? 3.5f : 2.5f);
            overlay.disc(at, radius + 1.5f, kOverlayEdge);
            overlay.disc(at, radius, c);
        }
    }
}
// ...and their text: each point's beats (showBeats, or the hovered point) and the juggler's
// number along each path.
void drawChoreographyPathLabels(ImDrawList* dl, const ChoreographyPaths& paths, int cycle, const Mat4& viewProj,
                                ImVec2 rectMin, ImVec2 rectSize, int selected, int hovered, bool showBeats,
                                const BodyOccluder& bodies) {
    dl->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);
    const float em = ImGui::GetFontSize();
    for (int pass = 0; pass < 2; ++pass) {
        auto mine = [&](int juggler) { return (juggler == selected) == (pass == 1); };
        const Rgba color = pass == 1 ? kPathSelected : kPathOther;
        for (size_t i = 0; i < paths.points.size(); ++i) {
            const PathPoint& p = paths.points[i];
            const bool hot = static_cast<int>(i) == hovered;
            if (!mine(p.juggler) || !(showBeats || hot) || bodies.hides(p.position)) continue;
            ImVec2 at;
            if (!projectToScreen(viewProj, p.position, rectMin, rectSize, &at)) continue;
            const std::string text = pathPointBeats(p, cycle);
            const ImVec2 size = ImGui::CalcTextSize(text.c_str());
            overlayText(dl, ImVec2(at.x - size.x - em * 0.35f, at.y - size.y - em * 0.1f), text.c_str(),
                        hot ? kPathHot : color);
        }
        for (size_t i = 0; i < paths.labelSpots.size(); ++i) {
            if (!mine(paths.labelJugglers[i]) || bodies.hides(paths.labelSpots[i])) continue;
            ImVec2 at;
            if (!projectToScreen(viewProj, paths.labelSpots[i], rectMin, rectSize, &at)) continue;
            char text[16];
            std::snprintf(text, sizeof(text), "J%d", paths.labelJugglers[i] + 1);
            const ImVec2 size = ImGui::CalcTextSize(text);
            const float padX = em * 0.3f, padY = em * 0.08f;
            const ImVec2 boxMin(at.x - size.x * 0.5f - padX, at.y - size.y * 0.5f - padY);
            const ImVec2 boxMax(at.x + size.x * 0.5f + padX, at.y + size.y * 0.5f + padY);
            dl->AddRectFilled(boxMin, boxMax, IM_COL32(28, 30, 36, pass == 1 ? 220 : 170), em * 0.3f);
            dl->AddRect(boxMin, boxMax, toImU32(color), em * 0.3f, 0, pass == 1 ? 1.5f : 1.0f);
            dl->AddText(ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), toImU32(color), text);
        }
    }
    dl->PopClipRect();
}

// Throw values over the props in flight (and on held 2s), when the ladder's throw values are
// on: "3", "4p", as on the ladder, in a small box edged in the prop's color.
// The style (color, dash) a prop is drawn with: its own, or with "color by orbit" its orbit's
// (styleOfBall maps prop ids to style indexes; empty means each prop its own).
int propStyle(const std::vector<int>& styleOfBall, int ball) {
    return ball >= 0 && ball < static_cast<int>(styleOfBall.size()) ? styleOfBall[static_cast<size_t>(ball)] : ball;
}

void drawPropValueLabels(ImDrawList* dl, const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin,
                         ImVec2 rectSize, ColorVisionMode colorVision, const std::vector<int>& styleOfBall,
                         bool relativeTargets) {
    dl->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);
    const float em = ImGui::GetFontSize();
    const int jugglers = static_cast<int>(scene.jugglers.size());
    // Above the prop: clear of a ball, or of a club or ring, whichever way it's turned.
    const float above = scene.prop == PropType::Ball ? kBallRadius + 0.05f : 0.34f;
    for (const BallState& b : scene.balls) {
        if (b.throwValue <= 0) continue;
        ImVec2 anchor;
        if (!projectToScreen(viewProj, b.center + Vec3(0.0f, above, 0.0f), rectMin, rectSize, &anchor)) continue;
        LoopThrow t;
        t.value = b.throwValue;
        t.dest = b.catcher;
        const std::string label = throwLabel(t, b.thrower, jugglers, relativeTargets);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const ImVec2 pad(em * 0.25f, em * 0.08f);
        const ImVec2 a(anchor.x - ts.x * 0.5f - pad.x, anchor.y - ts.y - 2.0f * pad.y);
        const ImVec2 c(anchor.x + ts.x * 0.5f + pad.x, anchor.y);
        dl->AddRectFilled(a, c, IM_COL32(20, 22, 28, 200), em * 0.25f);
        dl->AddRect(a, c, ballStyle(colorVision, propStyle(styleOfBall, b.ball)).color, em * 0.25f, 0, 1.0f);
        dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y), IM_COL32(232, 234, 240, 255), label.c_str());
    }
    dl->PopClipRect();
}

// ---- Tweakables panel ---------------------------------------------------------------------
//
// ("Tweakables" is a working name.) A small panel in the top-right corner of the juggler pane:
// tempo, dwell and, for passing patterns, the distance between the jugglers. Each has a slider with
// -/+ buttons for fine steps (Shift: bigger steps; hold a button to repeat), the mouse wheel
// over the slider for one step, and Ctrl+click or double-click on the slider to type a value.
// The panel collapses to a one-line readout.

constexpr double kMinTempo = 40.0, kMaxTempo = 420.0;  // 420 BPM: 7 throws a second
constexpr double kMinDwell = 0.1, kMaxDwell = 1.9;

struct FineSliderState {
    double beforeClick = 0.0;          // the value before the latest click on the slider
    double beforePreviousClick = 0.0;  // and before the one before that
};

// Returns true if the value changed.
bool fineSlider(const char* id, double* value, double minValue, double maxValue, double step,
                double bigStep, const char* format, float width, FineSliderState& state,
                const char* tooltip) {
    ImGui::PushID(id);
    bool changed = false;
    const double stepNow = ImGui::GetIO().KeyShift ? bigStep : step;
    auto nudge = [&](double direction) {
        // Steps land on the step grid (149.6 BPM + 1 -> 151, not 150.6).
        const double snapped = std::round(*value / step) * step;
        *value = std::clamp(snapped + direction * stepNow, minValue, maxValue);
        changed = true;
    };
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (ImGui::Button("-")) nudge(-1.0);
    ImGui::SameLine(0.0f, spacing);

    const double before = *value;
    float v = static_cast<float>(*value);
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat("##slider", &v, static_cast<float>(minValue), static_cast<float>(maxValue), format)) {
        *value = v;
        changed = true;
    }
    const ImGuiID sliderId = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state.beforePreviousClick = state.beforeClick;
        state.beforeClick = before;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        // Each click on a slider jumps the value to the mouse; a double-click means "let me type
        // it", so put back the value from before the first click and open the text field.
        *value = state.beforePreviousClick;
        changed = true;
        ImGui::ClearActiveID();
        GImGui->NavNextActivateId = sliderId;
        GImGui->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        nudge(ImGui::GetIO().MouseWheel > 0.0f ? 1.0 : -1.0);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", tooltip);

    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button("+")) nudge(1.0);
    ImGui::PopItemFlag();
    ImGui::PopID();
    return changed;
}

struct TweakablesPanelState {
    FineSliderState tempo, dwell, distance;
};

void resetTweakables(JuggleParams& params) {
    const JuggleParams defaults;
    params.bpm = defaults.bpm;
    params.dwellBeats = defaults.dwellBeats;
    params.distance = defaults.distance;
}

// Draws the panel with its top-right corner at `topRight`. `loop` is the pattern being shown
// (the distance row appears for passing patterns). Returns true if the user collapsed or
// expanded it (so the caller can remember that).
bool drawTweakablesPanel(JuggleParams& params, const JugglingLoop& loop, bool& collapsed, ImVec2 topRight,
                         TweakablesPanelState& state, bool distanceLocked, float* bottom) {
    const bool passing = loop.jugglers > 1;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(topRight, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    bool toggled = false;
    if (ImGui::Begin("Tweakables", nullptr, flags)) {
        if (collapsed) {
            if (ImGui::ArrowButton("##expand", ImGuiDir_Down)) toggled = true;
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (passing)
                ImGui::Text("%.0f BPM, dwell %.2f, %.2f m apart", params.bpm, params.dwellBeats,
                            passingDistance(loop, params));
            else
                ImGui::Text("%.0f BPM, dwell %.2f", params.bpm, params.dwellBeats);
            if (ImGui::IsItemClicked()) toggled = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Show the tweakables");
        } else {
            if (ImGui::ArrowButton("##collapse", ImGuiDir_Up)) toggled = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Hide the controls");
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tweakables");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                const double distance = params.distance;
                resetTweakables(params);
                if (distanceLocked) params.distance = distance;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip(distanceLocked ? "150 BPM, dwell 1.40 beats (the distance stays: the pattern has choreography)"
                                                 : "150 BPM, dwell 1.40 beats, automatic distance");

            const float labelWidth =
                std::max(em * 3.5f, ImGui::CalcTextSize(passing ? "Distance" : "Dwell").x +
                                        ImGui::GetStyle().ItemSpacing.x * 2.0f);
            const float sliderWidth = em * 14.0f;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Tempo");
            ImGui::SameLine(labelWidth);
            fineSlider("tempo", &params.bpm, kMinTempo, kMaxTempo, 1.0, 10.0, "%.0f BPM", sliderWidth,
                       state.tempo,
                       "Beats per minute: one beat is one throw, alternating hands.\n"
                       "-/+ or the mouse wheel: 1 BPM (Shift: 10).\n"
                       "Double-click or Ctrl+click to type a value.");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Dwell");
            ImGui::SameLine(labelWidth);
            fineSlider("dwell", &params.dwellBeats, kMinDwell, kMaxDwell, 0.05, 0.1, "%.2f beats", sliderWidth,
                       state.dwell,
                       "How long each hand holds a ball before throwing it, in beats.\n"
                       "Each hand throws every other beat, so this is out of 2; real\n"
                       "cascades are often around 1.3-1.6. Short throws (like 1s)\n"
                       "automatically get a shorter dwell so they still have some flight.\n"
                       "-/+ or the mouse wheel: 0.05 (Shift: 0.1).\n"
                       "Double-click or Ctrl+click to type a value.");
            if (passing) {
                // With choreography, the jugglers stand where it puts them (in meters), so the
                // distance can't change.
                ImGui::BeginDisabled(distanceLocked);
                ImGui::BeginGroup();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Distance");
                ImGui::SameLine(labelWidth);
                double distance = passingDistance(loop, params);
                const float autoWidth = ImGui::GetFrameHeight() + ImGui::CalcTextSize("Auto").x +
                                        ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
                if (fineSlider("distance", &distance, kMinPassingDistance, kMaxPassingDistance, 0.05, 0.25,
                               "%.2f m", sliderWidth - autoWidth, state.distance,
                               "How far apart the jugglers stand (body to body), in meters.\n"
                               "Automatic: farther for higher passes (about 1.8 m for 3s,\n"
                               "2.2 m for 4s).\n"
                               "-/+ or the mouse wheel: 0.05 m (Shift: 0.25 m).\n"
                               "Double-click or Ctrl+click to type a value."))
                    params.distance = std::clamp(distance, kMinPassingDistance, kMaxPassingDistance);
                ImGui::SameLine();
                bool automatic = params.distance <= 0.0;
                if (ImGui::Checkbox("Auto", &automatic))
                    params.distance = automatic ? 0.0 : defaultPassingDistance(loop);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !distanceLocked)
                    ImGui::SetTooltip("Pick the distance from the pattern's highest throw.\n"
                                      "Moving the slider turns this off.");
                ImGui::EndGroup();
                ImGui::EndDisabled();
                if (distanceLocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Unavailable: this pattern has choreography, which puts the jugglers\n"
                                      "where they stand. (View > Clear Choreography makes it available.)");
            }
        }
        if (bottom) *bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    }
    ImGui::End();
    if (toggled) collapsed = !collapsed;
    return toggled;
}

// ---- Choreography panel -------------------------------------------------------------------
//
// In choreography mode, under the Tweakables: the floor grid that spike marks (and juggler
// drops) snap to. Returns true if a setting changed (so the caller can save them).
constexpr int kMaxChoreographyLength = 1024;  // beats (longer routines: chain choreographies, later)

// A turn (see Turn), edited as it's typed: a direction, then numerator / denominator, each with
// - and + (CCW (o) CW ( )   [2] - +  /  [5] - +   (144°)). The fraction isn't reduced. True when
// *turn changes; *hovered (if given) is set to what the button under the mouse would make it.
bool turnEditor(const char* id, Turn* turn, Turn* hovered = nullptr) {
    ImGui::PushID(id);
    const float em = ImGui::GetFontSize();
    const float inner = ImGui::GetStyle().ItemInnerSpacing.x;
    const Turn was = *turn;
    int den = std::clamp(was.den, 2, kMaxTurnDen);
    int num = std::clamp(std::abs(was.num), 0, den - 1);
    bool clockwise = was.num < 0;
    auto make = [](int n, int d, bool cw) {
        Turn t;
        t.den = d;
        t.num = cw ? -n : n;
        return t;
    };
    bool changed = false;
    auto offer = [&](const Turn& would) {
        if (hovered && ImGui::IsItemHovered()) *hovered = would;
    };
    // Direction (none to choose for no turn or a half turn).
    ImGui::BeginDisabled(num == 0 || 2 * num == den);
    if (ImGui::RadioButton("CCW", !clockwise) && clockwise) {
        clockwise = false;
        changed = true;
    }
    offer(make(num, den, false));
    ImGui::SameLine();
    if (ImGui::RadioButton("CW", clockwise) && !clockwise) {
        clockwise = true;
        changed = true;
    }
    offer(make(num, den, true));
    ImGui::EndDisabled();
    // A number, with - and +.
    auto stepper = [&](const char* label, int* value, int lo, int hi, auto wouldMake) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 2.2f);
        int typed = *value;
        if (ImGui::InputInt(label, &typed, 0, 0)) {
            *value = std::clamp(typed, lo, hi);
            changed = true;
        }
        const float button = ImGui::GetFrameHeight();
        ImGui::PushID(label);  // (the two steppers' buttons apart)
        for (const int sign : {-1, 1}) {
            ImGui::SameLine(0.0f, inner);
            ImGui::PushID(sign);
            const int to = std::clamp(*value + sign, lo, hi);
            ImGui::BeginDisabled(to == *value);
            if (ImGui::Button(sign < 0 ? "-" : "+", ImVec2(button, button))) {
                *value = to;
                changed = true;
            }
            ImGui::EndDisabled();
            offer(wouldMake(to));
            ImGui::PopID();
        }
        ImGui::PopID();
    };
    stepper("##num", &num, 0, den - 1, [&](int n) { return make(n, den, clockwise); });
    ImGui::SameLine();
    ImGui::TextUnformatted("/");
    stepper("##den", &den, 2, kMaxTurnDen, [&](int d) { return make(std::min(num, d - 1), d, clockwise); });
    num = std::min(num, den - 1);
    const Turn now = make(num, den, clockwise);
    ImGui::SameLine();
    if (now.none()) {
        ImGui::TextDisabled("(none)");
    } else {
        const std::string shown = turnText(now);
        const size_t paren = shown.find('(');
        ImGui::TextDisabled("%s", paren == std::string::npos ? shown.c_str() : shown.c_str() + paren);
    }
    ImGui::PopID();
    if (changed && now != was) {
        *turn = now;
        return true;
    }
    return false;
}

struct ChoreographyPanelState {
    FineSliderState spacing, spokes, major;
    // Groups of spike marks: how many marks each has (Choreography::kMarkGroups of them), and what
    // was done with their chips this frame (-1: nothing): rested on, clicked (select it), Shift+
    // clicked (add it), emptied (its menu).
    std::vector<int> groupSizes;
    int hoveredGroup = -1, clickedGroup = -1, addedGroup = -1, emptiedGroup = -1;
};

// *newLength is set (else left alone) when the user asks for a new choreography length, and
// `phases`: the walking loop's times round (for the length in all).
// `period`: the pattern's period (Ctrl+-/+ steps the length a whole period).
bool drawChoreographyPanel(AppSettings& settings, ImVec2 topRight, ChoreographyPanelState& state, int length,
                           int* newLength, int phases, int period) {
    state.hoveredGroup = state.clickedGroup = state.addedGroup = state.emptiedGroup = -1;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(topRight, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.8f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    bool changed = false;
    static const char* const kGridNames[] = {"None", "Square", "Radial", "Triangles"};
    const int gridIndex = static_cast<int>(settings.floorGrid);
    if (ImGui::Begin("Choreography", nullptr, flags)) {
        if (settings.choreographyPanelCollapsed) {
            if (ImGui::ArrowButton("##expand", ImGuiDir_Down)) {
                settings.choreographyPanelCollapsed = false;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Choreography: %s grid", kGridNames[gridIndex]);
            if (ImGui::IsItemClicked()) {
                settings.choreographyPanelCollapsed = false;
                changed = true;
            }
        } else {
            if (ImGui::ArrowButton("##collapse", ImGuiDir_Up)) {
                settings.choreographyPanelCollapsed = true;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Hide the controls");
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Choreography");

            const float labelWidth0 = std::max(em * 3.5f, ImGui::CalcTextSize("Mid spokes").x + ImGui::GetStyle().ItemSpacing.x * 2.0f);
            // Its length: how many beats it takes to come round (whatever the siteswap's period).
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Length");
            ImGui::SameLine(labelWidth0);
            ImGui::SetNextItemWidth(em * 8.0f);
            int edited = length;
            const bool entered = ImGui::InputInt("beats##length", &edited, 1, std::max(1, period), ImGuiInputTextFlags_EnterReturnsTrue);
            if ((entered || ImGui::IsItemDeactivatedAfterEdit()) && edited != length)
                *newLength = std::clamp(edited, 1, kMaxChoreographyLength);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("How many beats the choreography takes before it repeats. It doesn't have to\n"
                                  "match the siteswap's period. Type a number and press Enter, or use -/+ for a\n"
                                  "beat (Ctrl+click -/+: a whole period of the pattern, %d beats). Longer adds empty\n"
                                  "beats at the end; shorter asks before dropping keyframes past the new end.\n"
                                  "(To add or take out a beat in the middle: right-click a beat in the C rows.)",
                                  std::max(1, period));
            // The walking loop (set in the grid's loop column): how long it is in all.
            if (phases > 1) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("");
                ImGui::SameLine(labelWidth0);
                ImGui::TextDisabled("%d x %d = %d beats in all", length, phases, length * phases);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("The walking loop (the column after the walking rows' last beat, under the\n"
                                      "siteswap grid) takes this many times round to bring everyone back.");
            }

            // Groups of spike marks: a chip for each (its key, how many marks), to rest on (shows
            // the group on the floor), click (selects it; Shift: adds it) or right-click (empty it).
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Groups");
                ImGui::SameLine(labelWidth0);
                bool any = false;
                for (int g = 0; g < static_cast<int>(state.groupSizes.size()); ++g) {
                    const int n = state.groupSizes[static_cast<size_t>(g)];
                    if (n <= 0) continue;
                    if (any) ImGui::SameLine();
                    any = true;
                    char label[24];
                    std::snprintf(label, sizeof(label), "%c\xc2\xb7%d##group%d", markGroupKey(g), n, g);
                    const Rgba c = markGroupColor(g);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.r * 0.45f, c.g * 0.45f, c.b * 0.45f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.r * 0.65f, c.g * 0.65f, c.b * 0.65f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.r * 0.8f, c.g * 0.8f, c.b * 0.8f, 1.0f));
                    if (ImGui::Button(label)) (ImGui::GetIO().KeyShift ? state.addedGroup : state.clickedGroup) = g;
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered()) {
                        state.hoveredGroup = g;
                        ImGui::SetTooltip("Group %c: %d spike mark%s. Click (or press %c) to select them; Shift+click (Shift+%c)\n"
                                          "adds them to the selection. Ctrl+%c makes the selection the group. Right-click to\n"
                                          "empty it. Hold Ctrl to see every group on the floor.",
                                          markGroupKey(g), n, n == 1 ? "" : "s", markGroupKey(g), markGroupKey(g), markGroupKey(g));
                    }
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Empty Group")) state.emptiedGroup = g;
                        ImGui::EndPopup();
                    }
                }
                if (!any) {
                    ImGui::TextDisabled("none (select marks, then Ctrl+1 ... Ctrl+0)");
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Groups of spike marks, to select again with one key: select marks (click,\n"
                                          "Shift+click) and press Ctrl+1 (... Ctrl+9, Ctrl+0). Then 1 selects them again,\n"
                                          "and Shift+1 adds them to a selection. Hold Ctrl to see the groups on the floor.");
                }
            }

            const float labelWidth = std::max(em * 3.5f, ImGui::CalcTextSize("Mid spokes").x + ImGui::GetStyle().ItemSpacing.x * 2.0f);
            const float sliderWidth = em * 14.0f;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Grid");
            ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(sliderWidth + 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x));
            int chosen = gridIndex;
            if (ImGui::Combo("##grid", &chosen, kGridNames, IM_ARRAYSIZE(kGridNames))) {
                settings.floorGrid = static_cast<FloorGridType>(chosen);
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("The grid spike marks snap to as you drag them (and jugglers you drop off\n"
                                  "the marks). Shift: drag freely. None: a 5 cm grid, not shown.\n"
                                  "Changing the grid never moves anything already placed.");
            if (settings.floorGrid != FloorGridType::None) {
                const bool radial = settings.floorGrid == FloorGridType::Radial;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(radial ? "Rings" : "Spacing");
                ImGui::SameLine(labelWidth);
                double spacing = settings.gridSpacing;
                if (fineSlider("spacing", &spacing, 0.25, 2.0, 0.05, 0.25, radial ? "every %.2f m" : "%.2f m", sliderWidth,
                               state.spacing,
                               radial ? "How far apart the rings are.\n-/+ or the mouse wheel: 0.05 m (Shift: 0.25 m)."
                                      : settings.floorGrid == FloorGridType::Square
                                            ? "How far apart the grid lines are.\n-/+ or the mouse wheel: 0.05 m (Shift: 0.25 m)."
                                            : "How far apart neighboring corners of the triangles are.\n"
                                              "-/+ or the mouse wheel: 0.05 m (Shift: 0.25 m).")) {
                    settings.gridSpacing = static_cast<float>(std::clamp(spacing, 0.25, 2.0));
                    changed = true;
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(radial ? "Mid rings" : "Mid lines");
                ImGui::SameLine(labelWidth);
                double every = settings.gridMajorEvery;
                if (fineSlider("major", &every, 2.0, 12.0, 1.0, 2.0, radial ? "every %.0f rings" : "every %.0f lines",
                               sliderWidth, state.major,
                               radial ? "Every so many rings are drawn a little heavier, for counting.\n"
                                        "-/+ or the mouse wheel: 1 (Shift: 2)."
                                      : "Every so many lines are drawn a little heavier, for counting (and the lines\n"
                                        "through the middle of the floor heaviest).\n"
                                        "-/+ or the mouse wheel: 1 (Shift: 2).")) {
                    settings.gridMajorEvery = static_cast<int>(std::clamp(std::lround(every), 2L, 12L));
                    changed = true;
                }
                if (radial) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Spokes");
                    ImGui::SameLine(labelWidth);
                    double spokes = settings.gridSpokes;
                    if (fineSlider("spokes", &spokes, 3.0, 32.0, 1.0, 4.0, "%.0f", sliderWidth, state.spokes,
                                   "How many lines come out from the middle.\n"
                                   "-/+ or the mouse wheel: 1 (Shift: 4).")) {
                        settings.gridSpokes = static_cast<int>(std::clamp(std::lround(spokes), 3L, 32L));
                        changed = true;
                    }
                    // Mid-weight spokes: every n-th, for the n that divide the spokes evenly.
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Mid spokes");
                    ImGui::SameLine(labelWidth);
                    const int spokeCount = std::clamp(settings.gridSpokes, 3, 32);
                    const int spokeMajor = effectiveSpokeMajor(settings);
                    char preview[32];
                    if (spokeMajor > 0)
                        std::snprintf(preview, sizeof(preview), "every %d (%d of them)", spokeMajor, spokeCount / spokeMajor);
                    else
                        std::snprintf(preview, sizeof(preview), "none");
                    ImGui::SetNextItemWidth(sliderWidth + 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x));
                    if (ImGui::BeginCombo("##spoke_major", preview)) {
                        if (ImGui::Selectable("none", spokeMajor == 0)) {
                            settings.gridSpokeMajor = 0;
                            changed = true;
                        }
                        for (int n = 2; n < spokeCount; ++n) {
                            if (spokeCount % n != 0) continue;
                            char item[32];
                            std::snprintf(item, sizeof(item), "every %d (%d of them)", n, spokeCount / n);
                            if (ImGui::Selectable(item, spokeMajor == n)) {
                                settings.gridSpokeMajor = n;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Every so many spokes drawn a little heavier. Only counts that divide the\n"
                                          "spokes evenly are offered (with 12 spokes: every 2, 3, 4 or 6).");
                    ImGui::SetCursorPosX(labelWidth);
                    if (ImGui::Checkbox("Turn half a spoke", &settings.gridHalfTurn)) changed = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Off: a spoke points at the default camera. On: the spokes are turned\n"
                                          "half a step, so that direction falls between two of them.");
                }
            }
        }
    }
    ImGui::End();
    return changed;
}

Vec3 colorToVec3(ImU32 c) {
    const ImVec4 f = ImGui::ColorConvertU32ToFloat4(c);
    return Vec3(f.x, f.y, f.z);
}

// A ghost of a juggler (the Walk Like... window's preview: where a choice would put them),
// drawn over everything so it shows even inside another juggler: a see-through body, ripples
// spreading out from its feet, a soft pulsing halo, sparks rising, and the path it would walk.
struct WalkGhost {
    bool show = false;
    JugglerPose pose;
    Mat4 placement;
    std::vector<Vec3> path;  // on the floor, round the whole choreography
};

void renderWalkGhost(Renderer& renderer, const Primitives& prims, const WalkGhost& ghost, const CameraView& camera,
                     const GLRect& rect, double time) {
    if (!ghost.show || rect.w <= 0 || rect.h <= 0) return;
    glViewport(rect.x, rect.y, rect.w, rect.h);
    // Over everything: only the ghost's own depth counts from here on.
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.w, rect.h);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    const Mat4 viewProj = cameraViewProj(camera, static_cast<float>(rect.w) / static_cast<float>(rect.h));
    const Vec3 color(0.55f, 0.90f, 1.00f);

    JugglerStyle style;
    style.bodyColor = color;
    style.handColor = color;
    renderer.beginScene(viewProj, {-0.4f, -1.0f, -0.6f});
    renderer.setGhostPass(Renderer::GhostPass::Depth);
    drawJuggler(renderer, prims, ghost.pose, ghost.placement, style);
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(time) * 5.0f);
    renderer.setGhostPass(Renderer::GhostPass::SeeThrough, 0.38f + 0.12f * pulse);
    drawJuggler(renderer, prims, ghost.pose, ghost.placement, style);
    renderer.setGhostPass(Renderer::GhostPass::Off);
    renderer.endScene();

    std::vector<GlowVertex> glow;
    const Vec3 feet = transformPoint(ghost.placement, Vec3(0.0f, 0.0f, 0.0f));
    // The path it would walk: dashed, just above the floor.
    if (ghost.path.size() >= 2) {
        std::vector<Vec3> raised;
        raised.reserve(ghost.path.size());
        for (const Vec3& p : ghost.path) raised.push_back(Vec3(p.x, 0.03f, p.z));
        const std::vector<float> fade(raised.size(), 1.0f);
        const float dash[] = {3.0f, 2.0f};
        appendRibbon(glow, raised, fade, color, 0.55f, 0.035f, camera.position, dash, 2, 0.05f);
    }
    // Ripples: rings spreading out from the feet and fading, one every 0.3 s.
    for (int r = 0; r < 3; ++r) {
        const float phase = static_cast<float>(std::fmod(time / 0.9 + r / 3.0, 1.0));
        const float radius = 0.25f + 0.75f * phase;
        std::vector<Vec3> ring;
        for (int i = 0; i <= 48; ++i) {
            const float a = static_cast<float>(i) / 48.0f * 6.2831853f;
            ring.push_back(Vec3(feet.x + radius * std::cos(a), 0.03f, feet.z + radius * std::sin(a)));
        }
        const std::vector<float> fade(ring.size(), 1.0f - phase);
        appendRibbon(glow, ring, fade, color, 0.7f, 0.04f, camera.position, nullptr, 0, 1.0f);
    }
    // A soft halo round the body, and sparks rising from the feet.
    const Vec3 forward = normalize(camera.target - camera.position);
    const Vec3 right = normalize(cross(forward, Vec3(0.0f, 1.0f, 0.0f)));
    const Vec3 up = cross(right, forward);
    auto blob = [&](Vec3 center, float radius, float alpha) {
        const int kSegments = 16;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = static_cast<float>(i) / kSegments * 6.2831853f;
            const float a1 = static_cast<float>(i + 1) / kSegments * 6.2831853f;
            const Vec3 p0 = center + (right * std::cos(a0) + up * std::sin(a0)) * radius;
            const Vec3 p1 = center + (right * std::cos(a1) + up * std::sin(a1)) * radius;
            glow.push_back({center.x, center.y, center.z, color.x, color.y, color.z, alpha});
            glow.push_back({p0.x, p0.y, p0.z, color.x, color.y, color.z, 0.0f});
            glow.push_back({p1.x, p1.y, p1.z, color.x, color.y, color.z, 0.0f});
        }
    };
    blob(feet + Vec3(0.0f, 1.05f, 0.0f), 0.85f, 0.16f + 0.10f * pulse);
    for (int k = 0; k < 10; ++k) {
        const float phase = static_cast<float>(std::fmod(time / 1.6 + k * 0.1, 1.0));
        const float angle = static_cast<float>(k) * 2.39996f;  // spread round (the golden angle)
        const float radius = 0.22f + 0.08f * static_cast<float>(k % 3);
        const Vec3 at = feet + Vec3(radius * std::cos(angle), 0.1f + 1.9f * phase, radius * std::sin(angle));
        blob(at, 0.035f, 0.9f * (1.0f - phase));
    }
    renderer.drawGlow(viewProj, glow);
}

// `selected` is the selected juggler (-1 for none): drawn in gold (lighter than the others,
// so it shows without color vision too), on a lighter disc on the floor.
void renderJugglerView(Renderer& renderer, const Primitives& prims, const PropMeshes& propMeshes,
                       const JugglerScene& scene, const CameraView& camera, int selected,
                       ColorVisionMode colorVision, const std::vector<int>& styleOfBall, const GLRect& rect) {
    if (rect.w <= 0 || rect.h <= 0) return;

    glViewport(rect.x, rect.y, rect.w, rect.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.w, rect.h);
    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 viewProj = cameraViewProj(camera, aspect);

    renderer.beginScene(viewProj, {-0.4f, -1.0f, -0.6f});

    // Floor disc, under all the jugglers.
    Vec3 middle;
    for (const JugglerState& j : scene.jugglers) middle = middle + j.position;
    if (!scene.jugglers.empty()) middle = middle * (1.0f / static_cast<float>(scene.jugglers.size()));
    float floorRadius = 1.2f;
    for (const JugglerState& j : scene.jugglers)
        floorRadius = std::max(floorRadius, length(j.position - middle) + 0.8f);
    renderer.drawMesh(prims.cylinder,
                      Mat4::translation({middle.x, -0.01f, middle.z}) *
                          Mat4::scaling({floorRadius, 0.01f, floorRadius}),
                      {0.30f, 0.32f, 0.36f});

    for (size_t j = 0; j < scene.jugglers.size(); ++j) {
        const JugglerState& juggler = scene.jugglers[j];
        if (static_cast<int>(j) == selected) {
            renderer.drawMesh(prims.cylinder,
                              Mat4::translation({juggler.position.x, -0.008f, juggler.position.z}) *
                                  Mat4::scaling({0.42f, 0.0105f, 0.42f}),
                              {0.58f, 0.62f, 0.70f});
        }
        JugglerStyle style;
        if (static_cast<int>(j) == selected) style.bodyColor = Vec3(0.94f, 0.85f, 0.56f);
        drawJuggler(renderer, prims, posedJuggler(juggler, scene.prop), jugglerMatrix(juggler), style);
    }

    // Props, in the same colors (and, on rings, dash patterns) as the ladder.
    for (const BallState& b : scene.balls) {
        const BallStyle style = ballStyle(colorVision, propStyle(styleOfBall, b.ball));
        drawProp(renderer, propMeshes, prims.sphere, b, colorToVec3(style.color), style.dash);
    }
    renderer.endScene();

    // Trails: a wide faint glow plus a narrow bright core, both fading toward the old end and
    // dashed like the ball's lines on the ladder (so balls stay distinguishable without color).
    std::vector<GlowVertex> glow;
    for (const Trail& t : scene.trails) {
        const BallStyle style = ballStyle(colorVision, propStyle(styleOfBall, t.ball));
        const Vec3 color = colorToVec3(style.color);
        const float* dash = nullptr;
        int dashCount = 0;
        dashPatternLengths(style.dash, &dash, &dashCount);
        const float dashUnit = 0.03f;  // meters per dash unit
        appendRibbon(glow, t.points, t.fade, color, 0.22f, 0.05f, camera.position, dash, dashCount, dashUnit);
        appendRibbon(glow, t.points, t.fade, color * 0.6f + Vec3(0.4f, 0.4f, 0.4f), 0.85f, 0.014f,
                     camera.position, dash, dashCount, dashUnit);
    }

    // Puffs of glowing smoke where props pop in or vanish (in a sketch): a few soft blobs in the
    // prop's color, drifting out and up as they fade, plus a brief bright flash at the start.
    {
        const Vec3 forward = normalize(camera.target - camera.position);
        const Vec3 right = normalize(cross(forward, Vec3(0.0f, 1.0f, 0.0f)));
        const Vec3 up = cross(right, forward);
        auto blob = [&](Vec3 center, float radius, Vec3 color, float alpha) {
            const int kSegments = 10;
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = static_cast<float>(i) / kSegments * 6.2831853f;
                const float a1 = static_cast<float>(i + 1) / kSegments * 6.2831853f;
                const Vec3 p0 = center + (right * std::cos(a0) + up * std::sin(a0)) * radius;
                const Vec3 p1 = center + (right * std::cos(a1) + up * std::sin(a1)) * radius;
                glow.push_back({center.x, center.y, center.z, color.x, color.y, color.z, alpha});
                glow.push_back({p0.x, p0.y, p0.z, color.x, color.y, color.z, 0.0f});
                glow.push_back({p1.x, p1.y, p1.z, color.x, color.y, color.z, 0.0f});
            }
        };
        for (const Puff& puff : scene.puffs) {
            const BallStyle style = ballStyle(colorVision, propStyle(styleOfBall, puff.ball));
            const Vec3 color = colorToVec3(style.color) * 0.55f + Vec3(0.45f, 0.45f, 0.45f);
            const float age = puff.age;
            const float fade = (1.0f - age) * std::sqrt(1.0f - age);
            const int kBlobs = 9;
            for (int k = 0; k < kBlobs; ++k) {
                const float angle = static_cast<float>(k) * 6.2831853f / kBlobs + 0.4f * std::sin(static_cast<float>(k) * 2.3f);
                const Vec3 dir = normalize(Vec3(std::cos(angle), 0.5f + 0.5f * std::sin(static_cast<float>(k) * 1.7f), std::sin(angle)));
                const Vec3 center = puff.center + dir * (0.05f + 0.28f * age) + Vec3(0.0f, 0.18f * age, 0.0f);
                blob(center, 0.06f + 0.11f * age, color, 0.7f * fade);
            }
            const float flashSpan = puff.appearing ? 0.35f : 0.25f;
            if (age < flashSpan) blob(puff.center, 0.14f, Vec3(1.0f, 1.0f, 1.0f) * 0.6f + color * 0.4f, 1.0f * (1.0f - age / flashSpan));
        }
    }
    renderer.drawGlow(viewProj, glow);
}

// The transport icons, drawn into a button's rectangle.
enum class TransportIcon { ToStart, StepBack, Play, Pause, StepForward };

void drawTransportIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, TransportIcon icon, ImU32 color) {
    const float h = max.y - min.y;
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float s = h * 0.28f;  // half-size of the icon
    auto triangleRight = [&](float cx) {
        dl->AddTriangleFilled(ImVec2(cx - s * 0.7f, c.y - s), ImVec2(cx + s * 0.8f, c.y),
                              ImVec2(cx - s * 0.7f, c.y + s), color);
    };
    auto triangleLeft = [&](float cx) {
        dl->AddTriangleFilled(ImVec2(cx + s * 0.7f, c.y - s), ImVec2(cx - s * 0.8f, c.y),
                              ImVec2(cx + s * 0.7f, c.y + s), color);
    };
    auto bar = [&](float cx) {
        dl->AddRectFilled(ImVec2(cx - s * 0.18f, c.y - s), ImVec2(cx + s * 0.18f, c.y + s), color);
    };
    switch (icon) {
        case TransportIcon::ToStart: bar(c.x - s * 1.05f); triangleLeft(c.x - s * 0.05f); triangleLeft(c.x + s * 0.95f); break;
        case TransportIcon::StepBack: bar(c.x - s * 0.75f); triangleLeft(c.x + s * 0.35f); break;
        case TransportIcon::Play: triangleRight(c.x + s * 0.1f); break;
        case TransportIcon::Pause: bar(c.x - s * 0.4f); bar(c.x + s * 0.4f); break;
        case TransportIcon::StepForward: triangleRight(c.x - s * 0.35f); bar(c.x + s * 0.75f); break;
    }
}

bool transportButton(const char* id, TransportIcon icon, float size) {
    const bool pressed = ImGui::Button(id, ImVec2(size * 1.6f, size));
    drawTransportIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                      icon, ImGui::GetColorU32(ImGuiCol_Text));
    return pressed;
}

// Playback clock. beat is fractional (beat 0 = the right hand's first throw) and may be
// negative after stepping back; the pattern repeats forever in both directions.
struct Playback {
    bool playing = true;
    double beat = 0.0;
    bool slow = false;          // slow motion (at AppSettings::slowRate)
};
constexpr double kStepBeats = 1.0 / 12.0;  // one arrow-key press; Shift steps a whole beat

// ---- The siteswap grid's messages (the grid itself is siteswap_grid.h)

// What the line under the siteswap grid says for a while: a problem (why something couldn't be
// done), or a note that isn't one ("Saved ...").
struct SiteswapBox {
    std::string note;            // why the last edit didn't happen
    double noteUntil = 0.0;      // (shown until then)
    std::string info;            // a note that isn't a problem ("Saved ..."), shown in plain text
    double infoUntil = 0.0;      // (shown until then)
};

// A throw described for the siteswap grid's tooltip: "J1, beat 3, right hand: 4p, a pass to J2's
// left hand, caught on beat 7." Beats count from 1, as on the ladder; each juggler's first
// throw is from the right hand (the left, if their hands are swapped).
std::string describeThrow(const JugglingLoop& loop, int juggler, int beat) {
    const LoopThrow& t = loop.at(juggler, beat);
    auto handName = [&loop](int j, int b) { return loop.rightHandBeat(j, b) ? "right" : "left"; };
    char text[160];
    int n = 0;
    if (loop.jugglers > 1) n = std::snprintf(text, sizeof(text), "J%d, ", juggler + 1);
    n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "beat %d, %s hand: ", beat + 1, handName(juggler, beat));
    char* rest = text + n;
    const size_t room = sizeof(text) - static_cast<size_t>(n);
    if (t.value == kOpenThrow) {
        std::snprintf(rest, room, "? (not decided yet)");
    } else if (t.value == 0) {
        std::snprintf(rest, room, "0, an empty hand");
    } else {
        const char digit = static_cast<char>(t.value < 10 ? '0' + t.value : 'a' + t.value - 10);
        const bool pass = t.dest != juggler;
        const int caught = beat + t.value;
        if (pass)
            std::snprintf(rest, room, "%cp, a pass to J%d's %s hand, caught on beat %d", digit, t.dest + 1,
                          handName(t.dest, caught), caught + 1);
        else if (caught % 2 == beat % 2)  // (the same juggler, so a swap changes nothing here)
            std::snprintf(rest, room, "%c, back to the same hand on beat %d", digit, caught + 1);
        else
            std::snprintf(rest, room, "%c, across to the %s hand on beat %d", digit, handName(juggler, caught), caught + 1);
    }
    return text;
}

// The keyboard shortcuts sheet (? or Help > Keyboard Shortcuts): every key and mouse action,
// by where it works.
void drawShortcutsWindow(bool* open) {
    struct Entry {
        const char* keys;
        const char* what;
    };
    struct Group {
        const char* title;
        std::vector<Entry> entries;
    };
    static const std::vector<Group> kGroups = {
        {"Anywhere (Space, Ctrl+S and Ctrl+Z work in the siteswap grid too)",
         {{"Space", "Play / pause"},
          {"Left / Right", "Step back / forward 1/12 of a beat (pauses)"},
          {"Shift+Left / Shift+Right", "Step a whole beat"},
          {"B", "Back to beat 1 (and the ladder to its start)"},
          {"S", "Slow motion on or off (the rate is beside the Slow button)"},
          {"A", "Metronome on or off: a tick every beat while playing, stronger on each count's first"},
          {"Ctrl+S", "Save (over the open one of My Patterns, else asks for a name)"},
          {"Ctrl+Shift+S", "Save As (to My Patterns, under a new name)"},
          {"Ctrl+Z", "Undo"},
          {"Ctrl+Y, Ctrl+Shift+Z", "Redo"},
          {"?", "This sheet"}}},
        {"What's shown",
         {{"V", "Throw values on the ladder and over the props"},
          {"O", "Color by orbit"},
          {"L", "Dim linked jugglers on the ladder"},
          {"C", "Collapse or expand every juggler's ladder strip"},
          {"Ctrl+1 ... Ctrl+6", "Collapse or expand that juggler's ladder strip (not in choreography: groups there)"},
          {"M", "Choreography (Movement, spike Marks): show the spike marks"},
          {"K", "Keyframes on the ladder: show their spike marks (M3)"},
          {"T", "Top view"},
          {"F", "Frame the selected juggler (no one selected: everyone)"}}},
        {"Juggler pane (mouse)",
         {{"Right-drag (or Alt+drag)", "Orbit the camera"},
          {"Middle-drag, Shift+right-drag", "Pan (drags the floor along; frees the camera)"},
          {"Wheel", "Zoom (free camera: move in / out)"},
          {"3D mouse (SpaceMouse)", "Grab the stage (frees the camera): spin, tilt: turn it round its middle; "
                                    "push away / pull: move back / in; slide: move the stage"},
          {"Home", "Reset the camera (back to automatic framing)"},
          {"Click a juggler", "Select them (click empty space to deselect)"},
          {"Double-click", "Frame that juggler (empty space: frame everyone)"},
          {"Rest on a club or ring (paused)", "Its throw and spin rate"},
          {"Choreography: drag a juggler", "A keyframe for them on this beat (drop on a mark to use it)"},
          {"Choreography: click a path's beat point", "Go to that beat, with that juggler selected"},
          {"Choreography: click a mark or its name", "Select it (Shift+click: add or remove marks; Esc clears)"},
          {"Choreography: Ctrl+1 ... Ctrl+9, Ctrl+0", "Make the selected marks that group (nothing selected: empty it)"},
          {"Choreography: 1 ... 9, 0", "Select that group of marks (Shift: add it to the selection)"},
          {"Choreography: hold Ctrl", "Show every group: each mark's groups under its name, each group's marks joined up"},
          {"Choreography: drag a mark", "Move it, and any others selected with it (its arrow: turn it); Shift: off the grid"},
          {"Choreography: right-click", "Add a mark (on the floor), or a mark's menu (without dragging)"}}},
        {"Ladder (mouse)",
         {{"Wheel", "Scroll through time"},
          {"Ctrl+wheel", "Zoom"},
          {"Home, Ctrl+0", "Reset the ladder view, and the playhead back to beat 1"},
          {"Click near a throw's end", "Pick it up; click again to put it down"},
          {"Shift+click a square", "Close the edit a loop later (adds or removes props)"},
          {"Esc, right-click", "Cancel an edit (or stop drawing a sketch)"},
          {"Click / drag the beat numbers", "Go to that beat / move the playhead"},
          {"Drag a keyframe diamond", "Move the keyframe to another beat"},
          {"Ctrl+drag a keyframe diamond", "Loiter: a copy on the same spot, up to the keyframes either side"},
          {"Right-click", "Menus: a throw, a beat line, a juggler's number, a keyframe"},
          {"Drag a keyframe diamond", "Move it to another beat"},
          {"Rest on a throw", "What it is (with clubs or rings, its spin)"}}},
        {"Siteswap grid",
         {{"Type a throw", "Replaces the cell's throw; the next throw typed goes in the next cell (531: three cells)"},
          {"p, then the target", "A pass (3p, 3p2, 3p+1); with two jugglers it's done at the p"},
          {"Arrows, Tab, Home / End", "Move (Shift+arrows: select); the greyed 3s of a count are skipped"},
          {"Backspace, Delete", "Clear the selected cells (blank: not decided yet)"},
          {"Ctrl+Up / Ctrl+Down", "The throw one higher / lower"},
          {"Ctrl+Shift+Up / Down", "Where it goes (self, then each juggler)"},
          {"Ctrl+T", "Swap where two throws land (the selection's ends, or the cell and the one before)"},
          {"Ctrl+R / Ctrl+L", "Rotate the pattern right / left (a whole count at a time)"},
          {"Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V", "Select all, copy, cut, paste (siteswap text; select all to paste a whole pattern)"},
          {"Ctrl+click a throw", "Make the cursor's juggler a copy of that juggler, from that throw on"},
          {"Right-click", "A cell: insert or delete columns; a row (T1...): add, remove, link"},
          {"The \"next\" column (T rows)", "Which T row whoever's on this one goes on to after the period (2); Backspace: its own"},
          {"Esc", "Stop typing a throw (puts the cell back), then give the keyboard back"},
          {"Rest on a throw", "What it does"}}},
        {"Choreography rows (under the throws)",
         {{"Type a mark's name", "A keyframe on that mark, that beat (two characters: 0b; or one, then Tab or an arrow: 5 is 05)"},
          {"~", "Turn the long way round on the way to this keyframe"},
          {"Backspace, Delete", "Clear the keyframe"},
          {"Arrows, Tab, Home / End", "Move; the stage goes to the cursor's beat (paused)"},
          {"Ctrl+click a beat", "The cursor's juggler walks like that juggler, from that beat on"},
          {"The \"next\" column (C rows)", "Which C row whoever's on this one goes on to after the beats written out, and the turn (2, 2r+1/4, r1/2)"},
          {"Right-click", "A cell: insert or delete a beat, Walk Like..., Unlink; the next column: a builder"}}},
    };
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 46.0f, ImGui::GetFontSize() * 40.0f), ImGuiCond_FirstUseEver);
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Keyboard Shortcuts", open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("On a Mac, Ctrl is Cmd. Press ? or Esc to close.");
        for (const Group& g : kGroups) {
            ImGui::SeparatorText(g.title);
            if (ImGui::BeginTable(g.title, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 17.0f);
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
                for (const Entry& e : g.entries) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(e.keys);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", e.what);
                }
                ImGui::EndTable();
            }
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
            *open = false;
    }
    ImGui::End();
}

}  // namespace

int runApp() {
    std::string missing;
    if (!gl::load(&missing)) {
        const std::string msg = "Missing OpenGL functions (driver too old?):\n" + missing;
        platformShowError("JuggleSim", msg.c_str());
        return 1;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout; nothing worth persisting yet
    // Keyboard navigation between widgets is left off: Space and the arrow keys drive playback.
    ImGui::StyleColorsDark();
    // Fonts: the classic pixel font for the UI (the first one added is the default), and the
    // scalable default font for larger text (the pattern's name over the juggler pane).
    io.Fonts->AddFontDefaultBitmap();
    ImFont* titleFont = io.Fonts->AddFontDefaultVector();
    constexpr float kTitleFontSize = 20.0f;

    ImGui_ImplOpenGL3_Init();  // (picks the right GLSL version for the platform)

    Renderer renderer;
    std::string error;
    if (!renderer.init(&error)) {
        platformShowError("JuggleSim: shader error", error.c_str());
        ImGui_ImplOpenGL3_Shutdown();
        return 1;
    }
    Primitives prims;
    prims.create();
    PropMeshes propMeshes;
    propMeshes.create();

    AppSettings settings = loadSettings();

    // The pattern (throw events) is the source of truth for juggling. The siteswap text box
    // generates it when the user types, and is rewritten from it when the pattern changes some
    // other way (e.g. the toolbar's period control). While the text is invalid, the last valid
    // pattern is kept but not shown.
    char siteswapText[4096] = "531";  // (room for 6 jugglers x 100 beats or so)
    SiteswapBox siteswapBox;
    SiteswapGridState siteswapGrid;  // the siteswap grid along the bottom (cursor, typing, dialogs)
    int patternCount = 0;            // its count directive ("4-count"), 0: none
    Playback playback;
    int siteswapHoverThrow = -1;  // the throw under the mouse in the text box (for the ladder)
    Siteswap parsed = parseSiteswap(siteswapText);
    Pattern pattern = patternFromSiteswap(parsed);
    bool patternValid = parsed.valid;
    // A sketch (a pattern with throws not decided yet, "?") is shown and edited on the ladder,
    // while the jugglers carry on with the last complete pattern (`pattern`), dimmed.
    bool sketching = false;
    JugglingLoop sketchLoop;
    // While drawing with the rubber band over a spot it can land in, the jugglers show what
    // clicking there would make.
    bool ladderPreview = false;
    JugglingLoop ladderPreviewLoop;
    // The pattern as last loaded, saved or started: File > New Pattern... and quitting offer to
    // save first if what's being juggled has changed since: its siteswap or its choreography
    // (tempo, dwell and distance don't count).
    std::string savedPatternText = siteswapText;
    std::string windowTitle;  // as last set (it's set only when it changes)
    bool savePromptRequested = false;
    bool promptForQuit = false;  // that prompt is for quitting (not for File > New Pattern...)
    bool newAfterSave = false;   // Save... was chosen in that prompt: start the new pattern once saved
    bool quitAfterSave = false;  // ...or quit once saved
    LadderEditState ladderEdit;  // drag-editing and pan/zoom state for the ladder
    PatternHistory history;
    history.current = siteswapText;

    JuggleParams juggleParams;
    juggleParams.prop = settings.prop;
    // Where the jugglers stand and walk: spike marks and keyframes (none: the default formation,
    // standing still). See choreography.h.
    Choreography choreography;
    // What the stage shows: the choreography, except while its walking loop doesn't work (a part
    // taken twice, or by nobody), when it stays as it last was that did (and the juggler pane is
    // tinted red), so where everyone is can still be seen while it's put right.
    Choreography stageChoreography;
    bool walkLoopBroken = false;
    std::string walkLoopProblem;  // ...and why (shown under the grid while it lasts)
    // The throwing loop (the siteswap grid's next column): whose throws each juggler takes up after
    // the period (-1: their own; see loop_ops.h). Saved with the pattern ("next=2,1").
    std::vector<int> throwNext;
    // ...the pattern played out with it this frame (and the walking loop's paths), and what the
    // stage plays: the same, except while it doesn't work (a part taken twice, props landing
    // together), when the stage keeps the last that did (tinted red), like the walking loop.
    PlayedLoop played;
    WalkPaths playPaths;
    JugglingLoop stagePlayLoop;
    bool throwLoopBroken = false;
    std::string throwLoopProblem;
    juggleParams.choreography = &choreography;  // (the stage's evaluation uses stageParams, below)
    // (For the save prompts: the choreography as last loaded, saved or started.)
    std::string savedChoreography;
    auto choreographyText = [&]() { return choreography.active() ? choreographyToText(choreography) : std::string(); };
    std::string savedThrowNext;
    auto markSaved = [&]() {
        savedPatternText = siteswapText;
        savedChoreography = choreographyText();
        savedThrowNext = throwNextText(throwNext);
    };
    bool choreographyMode = false;  // View > Choreography (M): spike marks shown and editable
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
    LoiterPopup loiter;
    // The plip (see sound.h): wanted this frame; and the notes already plipped for.
    bool plipWanted = false;
    // The metronome (A): the beat it last ticked for, and the tick wanted this frame (0: none, 1:
    // a beat, 2: a count's first beat).
    long metronomeBeat = 0;
    int tickWanted = 0;
    double lastNoteUntil = 0.0, lastLoiterErrorUntil = 0.0;
    int markDrag = -1;              // the spike mark being dragged, or -1
    int jugglerDrag = -1;           // the juggler being dragged to a new spot (a keyframe), or -1
    int viewPress = -1;             // the mouse button pressed in the juggler view (0 left, 1 right, 2 middle)
    float jugglerDragYaw = 0.0f;    // ...facing as they were when the drag started
    bool markTurning = false;       // ...by its arrow (turning it) rather than its middle
    bool markForKeyframe = false;   // ...and if it turns out to be a click, it's for a keyframe there
                                    // (not for selecting a juggler standing on it)
    int markMenuMark = -1;          // the floor's right-click menu: on this mark, or -1...
    int floorMenuJuggler = -1;      // ...and on this juggler, or -1
    Vec3 markMenuPoint;             // ...at this floor point
    CameraView lastCamera{};        // the 3D view as last drawn (for finding the floor under the mouse)
    float lastAspect = 1.0f;
    // The loop the jugglers juggle (the sketch, or the pattern; empty if neither).
    auto shownLoop = [&]() {
        return sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop());
    };
    // Spike marks are kept in meters for the distance they were placed at, and scale with it.
    // A spike mark's name ("0a"), or "?" for none.
    auto markNameOf = [&](int m) {
        return m >= 0 && m < static_cast<int>(choreography.marks.size()) ? choreography.marks[static_cast<size_t>(m)].name
                                                                          : std::string("?");
    };
    auto choreographyScale = [&](const JugglingLoop& loop) {
        if (choreography.reference <= 0.0 || loop.empty()) return 1.0f;
        return static_cast<float>(passingDistance(loop, juggleParams) / choreography.reference);
    };
    // Starts a choreography if there isn't one: a spike mark where each juggler stands now
    // (the default formation), facing as they do.
    // The choreography as ensureSpikeMarks made it (the default formation), while it's untouched;
    // once anything in it changes, the pattern's distance is fixed (see choreographyLocked).
    std::string untouchedChoreography;
    // The choreography's floor grid last put in the settings: one loaded (a pattern, undo) that
    // differs is put in place.
    std::string choreographyGridShown;
    auto ensureSpikeMarks = [&]() {
        if (choreography.active()) return;
        const JugglingLoop loop = shownLoop();
        if (loop.empty()) return;
        Choreography c;
        c.reference = passingDistance(loop, juggleParams);
        c.cycle = loop.period;  // its own length from now on (editing the siteswap doesn't change it)
        c.grid = floorGridText(settings);  // (made on the grid in use)
        choreographyGridShown = c.grid;
        for (int j = 0; j < loop.jugglers; ++j) {
            Vec3 position;
            float yaw = 0.0f;
            jugglerPlacement(loop, juggleParams, j, &position, &yaw);
            SpikeMark m;
            m.x = position.x;
            m.z = position.z;
            m.yaw = yaw;
            addSpikeMark(&c, m);
        }
        choreography = c;
        untouchedChoreography = choreographyToText(choreography);
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
    MarkChoice markChoice;
    // Spike marks selected: a click on one selects it (and no juggler), Shift+click adds one or
    // takes it away; dragging any of them moves them all, keeping their shape. Esc, or a click on
    // empty floor or a juggler, clears it. (Keyframes come from dragging a juggler onto a mark.)
    std::vector<int> selectedMarks;
    std::vector<std::pair<float, float>> markDragStart;  // ...where each was when the drag began (unscaled)
    bool markPressWasSelected = false;                   // ...whether the one pressed already was
    std::vector<int> selectedMarksSaid;                  // (the selection the status line last described)
    // Groups of marks (Choreography::groups): the one just selected by its key or chip (while the
    // selection is still exactly it: its lines show), and the chip the mouse rests on (-1: none).
    int selectedGroup = -1;
    int groupChipHovered = -1;
    // What of the groups the floor shows this frame: all of them (badges and lines) while Ctrl is
    // held; else the lines of the group under the mouse in the panel, or of the one selected.
    auto markGroupView = [&]() {
        MarkGroupView v;
        if (!choreographyMode || !choreography.active()) return v;
        if (ImGui::GetIO().KeyCtrl) {
            v.slots = choreography.groupsUsed();
            for (int g = 0; g < v.slots; ++g)
                if (!choreography.group(g).empty()) v.lines.push_back(g);
        } else if (groupChipHovered >= 0) {
            v.lines.push_back(groupChipHovered);
        } else if (selectedGroup >= 0 && !choreography.group(selectedGroup).empty()) {
            v.lines.push_back(selectedGroup);
        }
        return v;
    };
    auto markListText = [&](const std::vector<int>& marks) {
        std::string list;
        for (size_t i = 0; i < marks.size(); ++i)
            list += (i == 0 ? "" : (i + 1 == marks.size() ? " and " : ", ")) +
                    (marks[i] >= 0 && marks[i] < static_cast<int>(choreography.marks.size())
                         ? choreography.marks[static_cast<size_t>(marks[i])].name
                         : std::string("?"));
        return list;
    };
    // Makes group g (0-based) these marks (none: it's emptied). An undo step like any change.
    auto setMarkGroup = [&](int g, std::vector<int> marks) {
        if (g < 0 || g >= Choreography::kMarkGroups) return;
        std::sort(marks.begin(), marks.end());
        marks.erase(std::unique(marks.begin(), marks.end()), marks.end());
        if (static_cast<int>(choreography.groups.size()) <= g) choreography.groups.resize(static_cast<size_t>(g) + 1);
        choreography.groups[static_cast<size_t>(g)] = marks;
        while (!choreography.groups.empty() && choreography.groups.back().empty()) choreography.groups.pop_back();
        const char key = markGroupKey(g);
        siteswapBox.info = marks.empty() ? std::string("Group ") + key + " emptied."
                                         : std::string("Group ") + key + ": " + (marks.size() == 1 ? "mark " : "marks ") +
                                               markListText(marks) + ". Press " + key + " to select them again, Shift+" + key +
                                               " to add them to a selection; hold Ctrl to see the groups.";
        siteswapBox.infoUntil = ImGui::GetTime() + 5.0;
        selectedGroup = marks.empty() ? -1 : g;
    };
    // Renaming a spike mark (its menu's Rename...): two characters, 0-9 and a-z, unique.
    struct RenameMark {
        int mark = -1;
        bool requested = false;  // open the dialog this frame
        char text[3] = {0, 0, 0};
        std::string error;
    };
    RenameMark renameMark;
    // The loop column's builder (right-click a walking row's loop cell): whose part, and the turn.
    struct LoopBuilder {
        int juggler = -1;
        bool requested = false;
        Continuation k;
    };
    LoopBuilder loopBuilder;
    bool pressTakenByChoice = false;  // the current press in the juggler view picked a mark
    // The spike marks whose rings contain a floor point (world).
    auto marksAround = [&](float x, float z, float scale) {
        std::vector<int> found;
        for (int m = 0; m < static_cast<int>(choreography.marks.size()); ++m) {
            const SpikeMark& sm = choreography.marks[static_cast<size_t>(m)];
            if (std::hypot(x - sm.x * scale, z - sm.z * scale) <= kMarkRadius * scale) found.push_back(m);
        }
        return found;
    };
    auto openLoiterPopup = [&](int juggler, const Keyframe& arrival, ImVec2 at) {
        loiter.openRequested = true;
        loiter.juggler = juggler;
        loiter.arrival = arrival;
        loiter.onBeat = std::lround(playback.beat);
        loiter.at = at;
        loiter.text[0] = '\0';
        loiter.error.clear();
        loiter.errorUntil = 0.0;
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
    WalkLikeWindow walkLike;
    auto openWalkLike = [&](int juggler) {
        ensureSpikeMarks();
        walkLike.open = true;
        walkLike.juggler = juggler;
        walkLike.confirmLeader = -1;
    };
    // Unlinks a juggler who walks like another: their walking written out as their own keyframes,
    // so nothing moves (only the beats written out: the rotated repeats follow from them), on the
    // marks there, or new marks where the turn puts them somewhere there isn't one.
    auto unlinkWalk = [&](int j) {
        const JugglingLoop loop = shownLoop();
        const int cycle = loop.empty() ? 0 : choreographyCycle(loop, juggleParams);
        if (cycle <= 0 || !choreography.link(j).active()) return;
        std::vector<Keyframe> baked = effectiveKeyframes(choreography, j, cycle);
        baked.erase(std::remove_if(baked.begin(), baked.end(), [&](const Keyframe& k) { return k.beat >= cycle; }), baked.end());
        for (Keyframe& k : baked)
            if (k.mark < 0) k.mark = markForSpot(&choreography, k.x, k.z, k.yaw);
        setWalkLink(&choreography, j, WalkLink());
        if (static_cast<int>(choreography.keys.size()) <= j) choreography.keys.resize(static_cast<size_t>(j) + 1);
        choreography.keys[static_cast<size_t>(j)] = baked;
    };
    // The choreography's rows for the siteswap grid (see ChoreoGridView): per juggler, the mark
    // they arrive on each beat, loiters, and copies (walk links, rotated repeats) dim.
    auto choreoGridView = [&]() {
        ChoreoGridView view;
        const JugglingLoop loop = shownLoop();
        if (!choreography.active() || loop.empty()) return view;
        const int written = choreographyCycle(loop, juggleParams);
        const int n = loop.jugglers;
        const WalkLoop w = walkLoop(choreography, n);
        const int full = written * w.phases;
        if (written <= 0 || full <= 0) return view;
        view.active = true;
        view.written = written;
        view.full = full;
        view.loopProblem = w.valid ? std::string() : w.problem;
        for (const SpikeMark& m : choreography.marks) view.markNames.push_back(m.name);
        view.rows.resize(static_cast<size_t>(n));
        // A juggler's own keyframes in the beats written out (and their loiters).
        auto ownCells = [&](int j) {
            std::vector<ChoreoGridCell> cells(static_cast<size_t>(written));
            if (!choreography.hasKeys(j)) return cells;
            const std::vector<Keyframe>& keys = choreography.keys[static_cast<size_t>(j)];
            for (const Keyframe& k : keys) {
                if (k.beat < 0 || k.beat >= written) continue;
                ChoreoGridCell& cell = cells[static_cast<size_t>(k.beat)];
                cell.text = k.mark >= 0 && k.mark < static_cast<int>(choreography.marks.size())
                                ? choreography.marks[static_cast<size_t>(k.mark)].name
                                : std::string("?");
                if (k.longTurn) cell.text += "~";
            }
            // Loiters: between two keyframes on the same spot (round the end only when the loop
            // brings them back to their own part, unturned).
            const bool wraps = choreography.continuation(j).isDefault(j);
            for (size_t i = 0; i < keys.size(); ++i) {
                const bool last = i + 1 == keys.size();
                if (last && !wraps) break;
                const Keyframe& a = keys[i];
                const Keyframe& b = keys[last ? 0 : i + 1];
                if (keys.size() < 2 || !sameKeyframeSpot(choreography, a, b)) continue;
                const int end = last ? b.beat + written : b.beat;
                for (int beat = a.beat + 1; beat < end; ++beat) cells[static_cast<size_t>(beat % written)].stay = true;
            }
            return cells;
        };
        // Each row over the whole loop: its own path, then (dim) the same again each time round (the
        // rows are positions: who's on one is in its name); or, with a walk link, the leader's,
        // offset (dim; the turn is in the header).
        std::vector<char> state(static_cast<size_t>(n), 0);  // 0 not yet, 1 working on, 2 done
        std::function<const std::vector<ChoreoGridCell>&(int)> rowOf = [&](int j) -> const std::vector<ChoreoGridCell>& {
            ChoreoGridRow& row = view.rows[static_cast<size_t>(j)];
            if (state[static_cast<size_t>(j)] != 0) return row.cells;  // (done, or round in a circle)
            state[static_cast<size_t>(j)] = 1;
            row.cells.assign(static_cast<size_t>(full), ChoreoGridCell());
            const WalkLink l = choreography.link(j);
            if (l.active() && l.leader >= 0 && l.leader < n && !choreography.dependsOn(l.leader, j)) {
                const std::vector<ChoreoGridCell> lead = rowOf(l.leader);
                for (int beat = 0; beat < full && !lead.empty(); ++beat) {
                    row.cells[static_cast<size_t>(beat)] = lead[static_cast<size_t>(((beat + l.offset) % full + full) % full)];
                    row.cells[static_cast<size_t>(beat)].locked = true;
                }
                row.link = "@" + std::to_string(l.leader + 1) + "[" + std::to_string(l.offset) + "]" +
                           (l.turn.none() ? std::string() : " " + turnShort(l.turn));
            } else {
                const std::vector<ChoreoGridCell> part = ownCells(j);
                for (int p = 0; p < w.phases; ++p) {
                    for (int beat = 0; beat < written && beat < static_cast<int>(part.size()); ++beat) {
                        ChoreoGridCell cell = part[static_cast<size_t>(beat)];
                        cell.locked = p > 0 || cell.locked;
                        row.cells[static_cast<size_t>(p * written + beat)] = cell;
                    }
                }
            }
            state[static_cast<size_t>(j)] = 2;
            return row.cells;
        };
        for (int j = 0; j < n; ++j) {
            rowOf(j);
            ChoreoGridRow& row = view.rows[static_cast<size_t>(j)];
            row.loop = continuationText(choreography.continuation(j), j);
            row.loopBad = std::find(w.bad.begin(), w.bad.end(), j) != w.bad.end();
        }
        return view;
    };
    // (On means showing: the mode can be left on from a pattern with a choreography when one
    // without is loaded, and then the first press should turn it on, not off.)
    // Sets a juggler's loop column (see Continuation). An older file's one turn for everyone is
    // written out per juggler first. A loop column that doesn't work (a part taken twice, or by
    // nobody) is kept, and says so.
    auto setContinuation = [&](int j, const Continuation& k) {
        const JugglingLoop loop = shownLoop();
        const int n = std::max(loop.empty() ? 0 : loop.jugglers, j + 1);
        if (static_cast<int>(choreography.next.size()) < n || !choreography.repeat.none()) {
            std::vector<Continuation> all(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) all[static_cast<size_t>(i)] = choreography.continuation(i);
            choreography.next = all;
            choreography.repeat = Turn();
        }
        if (choreography.cycle == 0 && !loop.empty()) choreography.cycle = choreographyCycle(loop, juggleParams);
        choreography.next[static_cast<size_t>(j)] = k;
        const WalkLoop w = walkLoop(choreography, n);
        if (!w.valid) {
            siteswapBox.note = "The walking loop doesn't work: " + w.problem + ".";
            siteswapBox.noteUntil = ImGui::GetTime() + 8.0;
        }
    };
    // A beat of everyone's walking inserted before beat `beat` (or deleted): the keyframes after it
    // move along, and the Length with them.
    auto walkingBeat = [&](int beat, bool insert) {
        const JugglingLoop loop = shownLoop();
        if (loop.empty()) return;
        const int written = choreographyCycle(loop, juggleParams);
        if (!insert && written <= 1) return;
        for (std::vector<Keyframe>& keys : choreography.keys) {
            if (!insert)
                keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Keyframe& k) { return k.beat == beat; }), keys.end());
            for (Keyframe& k : keys)
                if (k.beat >= beat + (insert ? 0 : 1)) k.beat += insert ? 1 : -1;
        }
        choreography.cycle = std::clamp(written + (insert ? 1 : -1), 1, kMaxChoreographyLength);
    };
    auto toggleChoreographyMode = [&]() {
        choreographyMode = !(choreographyMode && choreography.active());
        if (choreographyMode) ensureSpikeMarks();
        markDrag = -1;
    };

    CameraControl camera;  // the juggler view's camera (a pattern can bring a free camera along)
    // The settings a library pattern can carry, as they are now (all of them).
    auto currentPatternSettings = [&]() -> PatternSettings {
        PatternSettings current;
        current.hasProp = current.hasTempo = current.hasDwell = current.hasDistance = current.hasChoreography = true;
        current.hasCount = true;
        current.count = patternCount;
        current.hasThrowNext = true;
        current.throwNext = throwNextText(throwNext);
        current.choreography = choreography.active() ? choreographyToText(choreography) : std::string();
        current.prop = juggleParams.prop;
        current.tempo = juggleParams.bpm;
        current.dwell = juggleParams.dwellBeats;
        current.distance = juggleParams.distance;
        return current;
    };
    // ...and for saving: with the camera too, if the user has placed it.
    auto settingsForSaving = [&]() -> PatternSettings {
        PatternSettings s = currentPatternSettings();
        if (camera.free) {
            s.hasCamera = true;
            s.camera[0] = camera.freePos.x;
            s.camera[1] = camera.freePos.y;
            s.camera[2] = camera.freePos.z;
            s.camera[3] = camera.freeYaw * 180.0 / kPi;
            s.camera[4] = camera.freePitch * 180.0 / kPi;
        }
        return s;
    };
    // Applies the settings a library pattern (or an undo step) carries; leaves the rest alone.
    auto applyPatternSettings = [&](const PatternSettings& apply) {
        if (apply.hasProp && apply.prop != settings.prop) {
            settings.prop = apply.prop;  // the Props menu choice is remembered between runs
            saveSettings(settings);
        }
        juggleParams.prop = settings.prop;
        if (apply.hasTempo) juggleParams.bpm = apply.tempo;
        if (apply.hasDwell) juggleParams.dwellBeats = apply.dwell;
        if (apply.hasDistance) juggleParams.distance = apply.distance;
        if (apply.hasCount) patternCount = apply.count;
        if (apply.hasThrowNext && !readThrowNext(apply.throwNext, &throwNext)) throwNext.clear();
        if (apply.hasCamera) {
            camera.free = true;
            camera.topView = false;
            camera.topBlend = 0.0f;
            camera.freePos = Vec3(static_cast<float>(apply.camera[0]), static_cast<float>(apply.camera[1]),
                                  static_cast<float>(apply.camera[2]));
            camera.freeYaw = static_cast<float>(apply.camera[3] * kPi / 180.0);
            camera.freePitch = std::clamp(static_cast<float>(apply.camera[4] * kPi / 180.0), -kMaxFreePitch, kMaxFreePitch);
        }
        if (apply.hasChoreography) {
            Choreography c;
            if (apply.choreography.empty() || !choreographyFromText(apply.choreography, &c)) c = Choreography();
            choreography = c;
            selectedMarks.clear();
            // The jugglers stand where it says, in meters at the distance it was made for.
            if (c.active() && c.reference > 0.0) juggleParams.distance = c.reference;
            markDrag = -1;
        }
    };

    // Settings changes become undo steps by polling: once the settings differ from the last
    // recorded ones and nothing has changed (and no control is held) for kSettingsSettleSeconds,
    // the change is recorded. Anything else that records a step records a pending settings
    // change first, so the steps stay in order.
    constexpr double kSettingsSettleSeconds = 0.6;
    PatternSettings recordedSettings = currentPatternSettings();
    PatternSettings lastSeenSettings = recordedSettings;
    double settingsChangedAt = 0.0;
    auto recordSettingsChange = [&]() {
        const PatternSettings now = currentPatternSettings();
        if (now != recordedSettings) history.recordWithSettings(history.current, recordedSettings, now);
        recordedSettings = lastSeenSettings = now;
    };
    auto pollSettingsChange = [&]() {
        const PatternSettings now = currentPatternSettings();
        if (now != lastSeenSettings) {
            lastSeenSettings = now;
            settingsChangedAt = ImGui::GetTime();
        }
        // (Held while the loiter popup is open: the arrival and the loiter are one undo step.)
        if (ImGui::IsAnyItemActive() || loiter.open || loiter.openRequested) settingsChangedAt = ImGui::GetTime();
        if (now != recordedSettings && ImGui::GetTime() - settingsChangedAt >= kSettingsSettleSeconds)
            recordSettingsChange();
    };
    // After undo/redo or a library load: what's showing is what's recorded.
    auto settingsRecorded = [&]() { recordedSettings = lastSeenSettings = currentPatternSettings(); };
    auto recordPattern = [&](const std::string& text) {
        recordSettingsChange();
        history.record(text);
    };
    // A throw being typed into the siteswap grid is shown as it's typed, and becomes an undo step
    // once it's finished; but a change made some other way (the ladder, undo, the library) can
    // come first. Each of those calls this, so what was typed is a step of its own and undoing the
    // change goes back to it.
    auto commitTyping = [&]() {
        if (history.current != siteswapText) recordPattern(siteswapText);
        siteswapGrid.editing = false;
    };

    // Replaces the pattern with the one written in `text` (used by undo/redo).
    auto showPatternText = [&](const std::string& text) {
        cancelLadderEdit(ladderEdit);
        std::snprintf(siteswapText, sizeof(siteswapText), "%s", text.c_str());
        parsed = parseSiteswap(siteswapText);
        patternValid = parsed.valid;
        sketching = parsed.sketch;
        if (parsed.valid) pattern = patternFromSiteswap(parsed);
        if (parsed.sketch) sketchLoop = parsed.loop;
    };
    // Makes a loop from the ladder the pattern (an edit, a throw drawn or deleted, beats added or
    // deleted): a complete one written at its shortest period, a sketch as it is. One undo step.
    // With linked jugglers, the change goes to every one of them (whichever was edited), or is
    // refused with the reason shown under the siteswap box.
    auto applyLoop = [&](const JugglingLoop& changed) {
        commitTyping();
        JugglingLoop l = changed;
        std::string why;
        if (!projectEdit(parsed.form, parsed.loop, changed, &l, &why)) {
            siteswapBox.note = "Can't: " + why;
            siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            return;
        }
        GridCount gridCount;
        gridCount.n = patternCount;
        if (openThrowCount(l) == 0) l = loopWithPeriod(l, countPeriodFor(loopShortestPeriod(l), gridCount));
        std::string text;
        // Written the way the pattern was typed: targets as they were, links as links.
        if (!loopToText(l, &text, &parsed.form)) return;
        // A count directive: the 3s between its beats stay as they are.
        SiteswapGrid gridBefore, gridAfter;
        if (gridCount.active() && gridFromText(siteswapText, &gridBefore) && gridFromText(text, &gridAfter) &&
            !countAllowsChange(gridBefore, gridAfter, gridCount, &why)) {
            siteswapBox.note = "Can't: " + why + ".";
            siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            return;
        }
        const Siteswap check = parseSiteswap(text);
        if (!check.valid && !check.sketch) return;  // shouldn't happen; keep what we have
        std::snprintf(siteswapText, sizeof(siteswapText), "%s", text.c_str());
        parsed = check;
        patternValid = check.valid;
        sketching = check.sketch;
        if (check.valid) pattern = patternFromSiteswap(check);
        if (check.sketch) sketchLoop = check.loop;
        recordPattern(text);
    };
    // Makes juggler j a copy of juggler `to` from to's beat `start` on ("Same as", from the ladder
    // or the grid), or writes a linked juggler's throws out again. One undo step.
    auto relink = [&](int j, int to, int start, bool unlink) {
        PatternForm newForm;
        JugglingLoop newLoop = parsed.loop;
        std::string why, text;
        bool ok = true;
        if (!unlink)
            ok = linkJuggler(parsed.form, parsed.loop, j, to, start, &newForm, &newLoop, &why, true);
        else
            newForm = unlinkJuggler(parsed.form, j);
        if (ok && !parsed.loop.empty() && loopToText(newLoop, &text, &newForm)) {
            commitTyping();
            cancelLadderEdit(ladderEdit);
            showPatternText(text);
            recordPattern(text);
        } else {
            siteswapBox.note = "Can't: " + (why.empty() ? std::string("the pattern has to be readable first") : why);
            siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
        }
    };
    auto linkJugglers = [&](int j, int to, int start) { relink(j, to, start, false); };
    auto unlinkJugglers = [&](int j) { relink(j, 0, 0, true); };
    auto undo = [&]() {
        if (ladderEdit.chain.active) {  // an open chain is just cancelled, like Esc
            cancelLadderEdit(ladderEdit);
            return;
        }
        commitTyping();  // a throw being typed is the step to undo
        recordSettingsChange();  // a change still settling is the one to undo
        if (history.undo.empty()) return;
        const HistoryStep step = history.undo.back();
        history.undo.pop_back();
        history.redo.push_back(step);
        history.current = step.before;
        if (step.before != step.after) showPatternText(step.before);
        if (step.settingsChanged) applyPatternSettings(step.settingsBefore);
        settingsRecorded();
    };
    auto redo = [&]() {
        commitTyping();
        recordSettingsChange();  // a change still settling is a new step, which ends redo
        if (history.redo.empty()) return;
        cancelLadderEdit(ladderEdit);
        const HistoryStep step = history.redo.back();
        history.redo.pop_back();
        history.undo.push_back(step);
        history.current = step.after;
        if (step.before != step.after) showPatternText(step.after);
        if (step.settingsChanged) applyPatternSettings(step.settingsAfter);
        settingsRecorded();
    };
    // The pattern library: built-in patterns, the user's own (saved, renamed and deleted from
    // the File menu), and the recently loaded ones.
    const std::vector<LibraryPattern> builtInPatterns = parsePatternLibrary(platformBuiltInPatternText());
    std::vector<LibraryPattern> myPatterns = loadUserPatterns();
    std::vector<RecentPattern> recentPatterns = loadRecentPatterns();
    for (size_t i = 0; i < recentPatterns.size();) {  // My Patterns deleted (e.g. by editing the file) go
        bool onDisk = !recentPatterns[i].mine;
        for (const LibraryPattern& saved : myPatterns)
            if (saved.displayName() == recentPatterns[i].pattern.displayName()) onDisk = true;
        if (onDisk)
            ++i;
        else
            recentPatterns.erase(recentPatterns.begin() + static_cast<std::ptrdiff_t>(i));
    }
    PatternMenuState juggleSimMenuState, myMenuState;

    // Loads a pattern from the library: its siteswap, and whatever settings it carries. One
    // undo step, which also puts the settings back. It goes to the top of Recent.
    // The one of My Patterns that's open (its name as shown), so File > Save can save over it;
    // empty if what's showing isn't one of them (new, typed, or from JuggleSim's patterns).
    std::string openPatternName;
    auto loadLibraryPattern = [&](const LibraryPattern& chosen, bool mine) {
        commitTyping();
        playback.beat = 0.0;  // a new pattern starts at beat 1
        recordSettingsChange();
        const PatternSettings before = currentPatternSettings();
        showPatternText(chosen.siteswap);
        choreography = Choreography();  // unless the pattern brings its own
        patternCount = 0;               // ...and no count directive, unless it brings one
        throwNext.clear();              // ...and no throwing loop, unless it brings one
        camera.free = false;            // ...and the automatic framing, unless it brings a camera
        applyPatternSettings(chosen.settings);
        history.recordWithSettings(siteswapText, before, currentPatternSettings());
        settingsRecorded();
        markSaved();
        addRecentPattern(&recentPatterns, chosen, mine);
        saveRecentPatterns(recentPatterns);  // not worth bothering the user if this fails
        openPatternName = mine ? chosen.displayName() : std::string();
    };

    SavePatternDialog saveDialog;
    // File > New Pattern...: a blank sketch (every throw "?") for some jugglers and period.
    struct NewPatternDialog {
        bool openRequested = false;
        int jugglers = 2;
        int period = 6;
        int count = 0;  // a count directive (0: none): blanks only on its beats, 3s between
        int walking = 0;  // the choreography's length, its own period (0: no choreography yet)
    } newPatternDialog;
    ManagePatternsWindow manageWindow;
    std::string fileError;  // shown in a message box when set
    // Names of known patterns by their siteswap (written at the shortest period, so "51" and a
    // typed "5151" both find the Shower). The user's own names win over the built-in ones.
    auto canonicalSiteswap = [](const Pattern& p) {
        std::string text;
        if (!patternToSiteswap(withPeriod(p, shortestPeriodBeats(p)), &text)) text.clear();
        return text;
    };
    std::map<std::string, std::string> patternNames;
    auto rebuildPatternNames = [&]() {
        patternNames.clear();
        const std::vector<LibraryPattern>* lists[] = {&builtInPatterns, &myPatterns};
        for (const std::vector<LibraryPattern>* list : lists) {
            for (const LibraryPattern& p : *list) {
                if (p.name.empty()) continue;
                const Siteswap known = parseSiteswap(p.siteswap);
                if (!known.valid) continue;
                const std::string key = canonicalSiteswap(patternFromSiteswap(known));
                if (!key.empty()) patternNames[key] = p.name;
            }
        }
    };
    rebuildPatternNames();
    auto saveMyPatterns = [&]() {
        rebuildPatternNames();
        if (!saveUserPatterns(myPatterns))
            fileError = "Couldn't write your patterns to " + userDataPath(L"my_patterns.txt").u8string() +
                        ". The change is kept until JuggleSim closes.";
    };

    auto openPatternIndex = [&]() -> int {
        if (openPatternName.empty()) return -1;
        for (size_t i = 0; i < myPatterns.size(); ++i)
            if (myPatterns[i].displayName() == openPatternName) return static_cast<int>(i);
        return -1;
    };
    // Saves what's showing to My Patterns as `name`: over the pattern of that name if there is
    // one (replace), else as a new one. It's then the open pattern.
    auto storeMyPattern = [&](const std::string& name, bool replace) {
        LibraryPattern saved;
        saved.siteswap = siteswapText;
        saved.name = name;
        saved.settings = settingsForSaving();
        saved.settings.hasDistance = (sketching ? sketchLoop.jugglers : pattern.jugglers) > 1;  // passing only
        classifyPattern(&saved);
        bool replaced = false;
        if (replace) {
            for (LibraryPattern& existing : myPatterns) {
                if (existing.displayName() == saved.displayName()) {
                    saved.otherSettings = existing.otherSettings;  // (settings from a newer JuggleSim)
                    existing = saved;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) myPatterns.push_back(saved);
        saveMyPatterns();
        markSaved();
        openPatternName = saved.displayName();
        addRecentPattern(&recentPatterns, saved, true);  // to the top, replacing any older copy
        saveRecentPatterns(recentPatterns);
    };
    // File > Save (Ctrl+S): over the open pattern, or (if none) asking for a name as Save As does.
    bool saveRequested = false, saveAsRequested = false;

    TweakablesPanelState tweakablesPanel;
    double spaceMouseQuietUntil = 0.0;     // ignore the mouse wheel until then (the 3D mouse is moving)
    double spaceMouseWheelBlocked = -10.0;  // when wheel input was last ignored for that
    int pendingChoreographyLength = 0;     // asking whether to shorten the choreography to this
    std::set<std::string> dismissedWarnings;  // (keys of) warnings the user has dismissed
    ChoreographyPanelState choreographyPanel;
    bool showImGuiDemo = false;
    bool showShortcuts = false;
    bool showSpaceMouseDiagnostics = false;  // Help > 3D Mouse Diagnostics  // the keyboard shortcuts sheet (? or Help > Keyboard Shortcuts)
    // Each throw's spin, for the ladder's tooltips (recomputed when the pattern or timing changes).
    std::vector<ThrowSpin> spinCache;
    JugglingLoop spinCacheLoop;
    JuggleParams spinCacheParams{-1.0, -1.0};
    bool showColorPreview = false;
    int pathPress = -1;  // a press on a choreography path's point (its index), to go there on release
    std::vector<GlowVertex> overlayTriangles;  // this frame's floor overlays (see OverlayBuilder)
    std::vector<float> jugglerLabelCenters;  // the juggler labels' screen x last frame (see drawJugglerLabels)
    // Orbiting a free camera: about this center, fixed for the length of each orbit (the
    // reticle shows it; the rest of the time it's worked out afresh, and shown faintly).
    Vec3 orbitCenter;
    bool cameraOrbiting = false;   // the mouse is orbiting the camera
    bool cameraPanning = false;    // ...or panning it
    bool viewPan = false;          // the press in the juggler view pans (middle, or Shift+right)
    bool viewOrbitLeft = false;    // ...a left press that orbits (Alt/Option held: for a trackpad)
    float panDepth = 3.0f;         // panning: how far away the floor under the mouse was
    double spaceOrbitUntil = 0.0;  // the 3D mouse is orbiting (until this time)
    // T: the top view (automatically framed), or back to automatic framing.
    auto toggleTopView = [&]() {
        if (camera.free) {
            camera.free = false;
            camera.topView = true;
        } else {
            camera.topView = !camera.topView;
        }
    };
    // Selection in the 3D view (passing patterns): the selected juggler, or -1. With
    // frameSelected the camera frames just that juggler.
    int selectedJuggler = -1;
    bool frameSelected = false;
    // Selects group g's marks (`add`: adds them to the selection), and no juggler.
    auto selectMarkGroup = [&](int g, bool add) {
        const std::vector<int>& members = choreography.group(g);
        const char key = markGroupKey(g);
        if (members.empty()) {
            siteswapBox.info = std::string("Group ") + key + " is empty: select spike marks (click, Shift+click) and press Ctrl+" +
                               key + " to make it.";
            siteswapBox.infoUntil = ImGui::GetTime() + 5.0;
            return;
        }
        if (!add) selectedMarks.clear();
        for (const int m : members)
            if (std::find(selectedMarks.begin(), selectedMarks.end(), m) == selectedMarks.end()) selectedMarks.push_back(m);
        selectedJuggler = -1;
        frameSelected = false;
        selectedGroup = add ? -1 : g;
    };
    Mat4 lastViewProj = Mat4::identity();  // the 3D view's camera as last drawn (for picking)
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
    constexpr double kExtentsSettleSeconds = 0.3;

    auto stepPlayback = [&](double beats) {
        playback.playing = false;
        playback.beat += beats;
    };
    // Back to beat 1 (playing or not, as it was).
    // Back to beat 1, with the ladder back to its default view (beat 1 at the top), playing or not.
    auto goToStart = [&]() {
        playback.beat = 0.0;
        resetLadderView(ladderEdit);
        ladderEdit.followPausedUntil = 0.0;
    };

    bool done = false;
    bool closePending = false;  // the window's close box was clicked (handled once it's showing)
    // Quitting: right away, or first asking whether to save the pattern if it has changed.
    auto requestQuit = [&]() {
        if ((patternValid || sketching) && (savedPatternText != siteswapText || savedChoreography != choreographyText() || savedThrowNext != throwNextText(throwNext))) {
            promptForQuit = true;
            savePromptRequested = true;
        } else {
            done = true;
        }
    };
    while (!done) {
        if (!platformPollEvents()) break;
        if (platformTakeCloseRequest()) closePending = true;
        if (platformIsMinimized()) {
            platformSleepMilliseconds(10);
            continue;
        }
        if (closePending) {
            closePending = false;
            requestQuit();
        }

        ImGui_ImplOpenGL3_NewFrame();
        platformNewFrame();
        ImGui::NewFrame();

        // --- Playback clock ---
        if (playback.playing)
            playback.beat += static_cast<double>(io.DeltaTime) * juggleParams.bpm / 60.0 *
                             (playback.slow ? static_cast<double>(settings.slowRate) : 1.0);
        // The metronome: a tick as each beat starts while playing (not while scrubbing, stepping
        // or paused), a stronger one on the first beat of each count (or of the pattern, without
        // a count directive).
        {
            const long beatNow = static_cast<long>(std::floor(playback.beat));
            if (playback.playing && settings.metronome && beatNow != metronomeBeat) {
                const long every = patternCount >= 2 ? patternCount : std::max(1, parsed.loop.period);
                tickWanted = ((beatNow % every) + every) % every == 0 ? 2 : 1;
            }
            metronomeBeat = beatNow;
        }

        // --- Keyboard shortcuts (not while typing: the text box has its own undo, etc.) ---
        // While the siteswap grid has the keyboard, keys are its (throws are letters too), so the
        // single-key shortcuts are off, as they are for a text box; Space, Ctrl+S and undo work.
        const bool gridHasKeys = siteswapGridHasKeyboard(siteswapGrid);
        if (gridHasKeys) io.WantTextInput = true;
        // Ctrl+S / Ctrl+Shift+S: Save / Save As (even while typing a siteswap).
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
            commitTyping();
            (io.KeyShift ? saveAsRequested : saveRequested) = true;
        }
        if ((!io.WantTextInput || gridHasKeys) && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift) undo();
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift)) redo();
            if (choreographyMode && choreography.active()) {
                // Choreography: Ctrl+1 ... Ctrl+9, Ctrl+0 make the selected spike marks a group
                // (nothing selected: empty it).
                for (int g = 0; g < Choreography::kMarkGroups; ++g)
                    if (ImGui::IsKeyPressed(g == 9 ? ImGuiKey_0 : static_cast<ImGuiKey>(ImGuiKey_1 + g), false))
                        setMarkGroup(g, selectedMarks);
            } else {
                // Ctrl+1 to Ctrl+6: collapse or expand that juggler's ladder strip.
                const int jugglers = sketching ? sketchLoop.jugglers : (patternValid ? pattern.jugglers : 0);
                for (int j = 0; j < std::min(jugglers, kMaxJugglers); ++j)
                    if (jugglers > 1 && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + j), false))
                        ladderEdit.collapsed[static_cast<size_t>(j)] = !ladderEdit.collapsed[static_cast<size_t>(j)];
            }
        }
        // The 3D mouse (SpaceMouse) always moves the juggler camera, whatever has the keyboard;
        // the cap's deflection sets a speed. (Applied to the camera further down, once the
        // scene is known.)
        const SpaceMouseState spaceMouse = platformSpaceMouse();
        // 3Dconnexion's own software may also send scrolling when the cap moves (its default
        // profile turns a tilt into a zoom). While the cap is moving, and for a moment after,
        // mouse-wheel input is ignored, so it doesn't zoom or scroll on top of what we do.
        {
            bool moving = false;
            for (int i = 0; i < 3; ++i)
                moving = moving || std::fabs(spaceMouse.move[i]) > 0.06f || std::fabs(spaceMouse.turn[i]) > 0.06f;
            if (spaceMouse.present && moving) spaceMouseQuietUntil = ImGui::GetTime() + 0.3;
            if (ImGui::GetTime() < spaceMouseQuietUntil) {
                if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) spaceMouseWheelBlocked = ImGui::GetTime();
                io.MouseWheel = io.MouseWheelH = 0.0f;
            }
        }
        // ?: the keyboard shortcuts (typed as a character, so it's Shift+/ on most keyboards).
        if (!io.WantTextInput)
            for (const ImWchar c : io.InputQueueCharacters)
                if (c == '?') showShortcuts = !showShortcuts;
        if (gridHasKeys && !io.KeyCtrl && !loiter.open && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            playback.playing = !playback.playing;
        if (!io.WantTextInput && !io.KeyCtrl && !loiter.open) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) playback.playing = !playback.playing;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) goToStart();
            if (ImGui::IsKeyPressed(ImGuiKey_S, false)) playback.slow = !playback.slow;
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
                settings.metronome = !settings.metronome;
                saveSettings(settings);
            }
            // Choreography: 1 ... 9, 0 select a group of spike marks (Shift: add it).
            if (choreographyMode && choreography.active() && !markChoice.active)
                for (int g = 0; g < Choreography::kMarkGroups; ++g)
                    if (ImGui::IsKeyPressed(g == 9 ? ImGuiKey_0 : static_cast<ImGuiKey>(ImGuiKey_1 + g), false))
                        selectMarkGroup(g, io.KeyShift);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
            // F: frame the selected juggler, or everyone if no one is selected.
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                frameSelected = selectedJuggler >= 0;
                camera.free = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                settings.colorByOrbit = !settings.colorByOrbit;
                saveSettings(settings);
            }
            // C: collapse all the jugglers' ladder strips (or expand them, if all are collapsed).
            // L: dim linked jugglers' throws on the ladder (or not).
            if (ImGui::IsKeyPressed(ImGuiKey_L, false) && parsed.form.hasLinks()) {
                settings.dimLinked = !settings.dimLinked;
                saveSettings(settings);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                const int jugglers = sketching ? sketchLoop.jugglers : (patternValid ? pattern.jugglers : 0);
                if (jugglers > 1) toggleCollapseAll(ladderEdit, jugglers);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                settings.showThrowValues = !settings.showThrowValues;
                saveSettings(settings);
            }
            // M: choreography (Movement, spike Marks: shown and editable); K: the keyframes'
            // spike marks on the ladder; T: the top view.
            if (ImGui::IsKeyPressed(ImGuiKey_M, false)) toggleChoreographyMode();
            if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
                settings.showKeyframeMarks = !settings.showKeyframeMarks;
                saveSettings(settings);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_T, false)) toggleTopView();
        }

        // --- Main menu bar ---
        float menuHeight = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Pattern...")) {
                    if ((patternValid || sketching) &&
                        (savedPatternText != siteswapText || savedChoreography != choreographyText() || savedThrowNext != throwNextText(throwNext))) {
                        promptForQuit = false;
                        savePromptRequested = true;
                    } else {
                        newPatternDialog.openRequested = true;
                    }
                }
                ImGui::Separator();
                // (A chosen pattern is copied before loading it: loading changes the Recent list
                // the choice may point into.)
                if (ImGui::BeginMenu("JuggleSim Patterns")) {
                    const PatternChoice choice =
                        drawJuggleSimPatternsMenu(builtInPatterns, juggleSimMenuState, settings.colorVision);
                    if (choice.pattern) {
                        const LibraryPattern chosen = *choice.pattern;
                        loadLibraryPattern(chosen, choice.mine);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("My Patterns")) {
                    const PatternChoice choice =
                        drawMyPatternsMenu(myPatterns, recentPatterns, myMenuState, settings.colorVision);
                    if (choice.pattern) {
                        LibraryPattern chosen = *choice.pattern;
                        if (choice.mine) {  // from Recent: the saved version, if it's still there
                            for (const LibraryPattern& saved : myPatterns) {
                                if (saved.displayName() == chosen.displayName()) {
                                    chosen = saved;
                                    break;
                                }
                            }
                        }
                        loadLibraryPattern(chosen, choice.mine);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                {
                    const bool open = openPatternIndex() >= 0;
                    const std::string saveLabel = open ? "Save \"" + shortMenuText(openPatternName) + "\"" : std::string("Save...");
                    if (ImGui::MenuItem(saveLabel.c_str(), "Ctrl+S", false, patternValid || sketching))
                        saveRequested = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip(open ? "Save the changes to this pattern in My Patterns."
                                               : "Save to My Patterns (it isn't one of them yet, so this asks for a name).");
                    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, patternValid || sketching))
                        saveAsRequested = true;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Save to My Patterns under a new name (or over another one).");
                }
                ImGui::MenuItem("Manage My Patterns...", nullptr, &manageWindow.open);
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) requestQuit();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                    !history.undo.empty() || ladderEdit.chain.active))
                    undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !history.redo.empty())) redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Playback")) {
                if (ImGui::MenuItem(playback.playing ? "Pause" : "Play", "Space"))
                    playback.playing = !playback.playing;
                if (ImGui::MenuItem("Step Forward", "Right")) stepPlayback(kStepBeats);
                if (ImGui::MenuItem("Step Back", "Left")) stepPlayback(-kStepBeats);
                if (ImGui::MenuItem("Step Forward One Beat", "Shift+Right")) stepPlayback(1.0);
                if (ImGui::MenuItem("Step Back One Beat", "Shift+Left")) stepPlayback(-1.0);
                if (ImGui::MenuItem("Go to Beat 1", "B")) goToStart();
                if (ImGui::MenuItem("Slow Motion", "S", playback.slow)) playback.slow = !playback.slow;
                if (ImGui::MenuItem("Metronome", "A", settings.metronome)) {
                    settings.metronome = !settings.metronome;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("A tick on every beat while playing, a stronger one on the first beat of each count.");
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Tweakables")) resetTweakables(juggleParams);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Tempo 150 BPM, dwell 1.40 beats, automatic distance");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Props")) {
                for (int i = 0; i < static_cast<int>(PropType::Count); ++i) {
                    const PropType prop = static_cast<PropType>(i);
                    if (ImGui::MenuItem(propTypeName(prop), nullptr, settings.prop == prop) &&
                        settings.prop != prop) {
                        settings.prop = prop;
                        juggleParams.prop = prop;
                        saveSettings(settings);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Ladder View", "Home over ladder", false, !ladderViewIsDefault(ladderEdit)))
                    resetLadderView(ladderEdit);
                if (ImGui::MenuItem("Throw Values", "V", settings.showThrowValues)) {
                    settings.showThrowValues = !settings.showThrowValues;
                    saveSettings(settings);
                }
                if (ImGui::MenuItem("Reset Layout", nullptr, false, settings.ladderFraction != 0.5f)) {
                    settings.ladderFraction = 0.5f;
                    saveSettings(settings);
                }
                if (ImGui::MenuItem("Reset Camera", "Home over juggler")) {
                    resetCamera(camera);
                    frameSelected = false;
                }
                if (ImGui::MenuItem("Frame All", nullptr, false, frameSelected || camera.free)) {
                    frameSelected = false;
                    camera.free = false;
                }
                if (ImGui::MenuItem("Frame Selected", "F", false, selectedJuggler >= 0)) {
                    frameSelected = true;
                    camera.free = false;
                }
                if (ImGui::MenuItem("Top View", "T", camera.topView)) toggleTopView();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Look straight down at the floor (for placing spike marks and jugglers).");
                ImGui::Separator();
                if (ImGui::MenuItem("Choreography", "M", choreographyMode && choreography.active())) toggleChoreographyMode();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Shows the spike marks on the floor, to move, turn and put jugglers on.\n"
                                      "Pause on a beat, then drag a juggler to where they should be then (onto a\n"
                                      "mark, or anywhere). Click marks to select them (Shift+click: more) and drag one\n"
                                      "to move them all. T: the top view.");
                if (ImGui::MenuItem("Keyframe Marks", "K", settings.showKeyframeMarks)) {
                    settings.showKeyframeMarks = !settings.showKeyframeMarks;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("On the ladder: \"M3\" beside each keyframe on a spike mark.");
                if (ImGui::MenuItem("Path Beat Numbers", nullptr, settings.pathBeatNumbers, choreographyMode)) {
                    settings.pathBeatNumbers = !settings.pathBeatNumbers;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Choreography: number every beat along the jugglers' paths.\n"
                                      "Off: only the point under the mouse is numbered.");
                if (ImGui::MenuItem("Clear Choreography", nullptr, false, choreography.active())) {
                    choreography = Choreography();
                    choreographyMode = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Color by Orbit", "O", settings.colorByOrbit)) {
                    settings.colorByOrbit = !settings.colorByOrbit;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("One color per orbit (the throws a group of props travel round)\n"
                                      "instead of one per prop. Sketches are always colored by path.");
                {
                    const int jugglers = sketching ? sketchLoop.jugglers : (patternValid ? pattern.jugglers : 0);
                    const bool expand = allStripsCollapsed(ladderEdit, jugglers);
                    if (ImGui::MenuItem(expand ? "Expand All Jugglers" : "Collapse All Jugglers", "C", false, jugglers > 1))
                        toggleCollapseAll(ladderEdit, jugglers);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Narrows every juggler's strip on the ladder (their throws still show),\n"
                                          "or widens them all again if they're all narrow.");
                }
                if (ImGui::MenuItem("Dim Linked Jugglers", "L", settings.dimLinked, parsed.form.hasLinks())) {
                    settings.dimLinked = !settings.dimLinked;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Mutes the throws of jugglers who are links (@2[3]: another juggler's\n"
                                      "throws) on the ladder, so the parts written out stand out.");
                if (ImGui::BeginMenu("Color Vision")) {
                    for (int i = 0; i < static_cast<int>(ColorVisionMode::Count); ++i) {
                        const ColorVisionMode mode = static_cast<ColorVisionMode>(i);
                        if (ImGui::MenuItem(colorVisionModeName(mode), nullptr,
                                            settings.colorVision == mode) &&
                            settings.colorVision != mode) {
                            settings.colorVision = mode;
                            saveSettings(settings);
                        }
                    }
                    ImGui::Separator();
                    ImGui::MenuItem("Preview all modes...", nullptr, &showColorPreview);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Sounds", nullptr, settings.sounds)) {
                    settings.sounds = !settings.sounds;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("A short plip when something you just did is refused, or breaks the pattern.");
                ImGui::Separator();
                ImGui::MenuItem("ImGui demo window", nullptr, &showImGuiDemo);
                ImGui::EndMenu();
            }
            if (spaceMouse.present && ImGui::BeginMenu("3D Mouse")) {
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                if (ImGui::SliderFloat("Speed", &settings.spaceMouseSpeed, 0.25f, 4.0f, "%.2fx",
                                       ImGuiSliderFlags_Logarithmic))
                    saveSettings(settings);
                ImGui::TextDisabled("Grab the stage (it makes the camera free). Spin: orbit.\n"
                                    "Tilt toward / away: turn it to see it from above / below.\n"
                                    "Push away / pull toward you: move back / in.\n"
                                    "Slide sideways or up / down: the stage moves with it.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("Keyboard Shortcuts", "?", &showShortcuts);
                ImGui::MenuItem("3D Mouse Diagnostics", nullptr, &showSpaceMouseDiagnostics, spaceMouse.present);
                ImGui::EndMenu();
            }
            menuHeight = ImGui::GetWindowSize().y;
            ImGui::EndMainMenuBar();
        }

        // --- Pane layout ---
        const ImGuiStyle& style = ImGui::GetStyle();
        const float W = io.DisplaySize.x;
        const float H = io.DisplaySize.y;
        const float top = menuHeight;
        // The ladder pane's width: the user's share of the window (dragging the divider), but
        // never so narrow that either pane is unusable.
        const float minPaneWidth = std::min(ImGui::GetFontSize() * 16.0f, W * 0.5f);
        const float leftWidth = std::floor(std::clamp(W * settings.ladderFraction, minPaneWidth, W - minPaneWidth));
        // A choreography that comes with its floor grid (loaded, or undone to) puts it in place.
        if (choreography.active() && !choreography.grid.empty() && choreography.grid != choreographyGridShown) {
            if (readFloorGridText(choreography.grid, &settings)) saveSettings(settings);
            choreographyGridShown = choreography.grid;
        }
        // The siteswap entry is a pane of its own along the bottom, the full width of the window
        // (long passing patterns need the room); the ladder and juggler panes sit above it.
        int gridRows = 1;
        {
            SiteswapGrid shownGrid;
            if (gridFromText(siteswapText, &shownGrid)) gridRows = std::max(1, shownGrid.jugglers());
        }
        const float entryHeight =
            siteswapGridHeight(gridRows, choreography.active() ? gridRows : 0) + style.WindowPadding.y * 2.0f;
        const float bodyBottom = H - entryHeight;
        const ImGuiWindowFlags paneFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;

        // The throwing loop played out (see loop_ops.h): what's thrown, beat by beat, a pass going to
        // whoever walks its target's path (the walking loop, as it last worked) when it lands.
        const JugglingLoop writtenLoop = sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop());
        {
            playPaths = WalkPaths();
            const Choreography& c = walkLoopBroken ? stageChoreography : choreography;
            if (c.active() && !writtenLoop.empty()) {
                const WalkLoop w = walkLoop(c, writtenLoop.jugglers);
                if (w.valid && w.phases > 1) {
                    playPaths.cycle = c.cycle > 0 ? c.cycle : choreographyCycle(writtenLoop, juggleParams);
                    playPaths.path = w.role;
                }
            }
            const ThrowLoopPlan plan = throwLoopPlan(throwNext, writtenLoop.jugglers);
            played = playedLoop(writtenLoop, plan.valid ? plan : ThrowLoopPlan(), playPaths);
            throwLoopBroken = !writtenLoop.empty() && (!plan.valid || !played.ok);
            throwLoopProblem = !plan.valid ? plan.problem : played.problem;
            if (!throwLoopBroken || stagePlayLoop.jugglers != writtenLoop.jugglers || writtenLoop.empty())
                stagePlayLoop = played.loop;
        }

        // Ladder diagram (top of left half).
        ImGui::SetNextWindowPos(ImVec2(0.0f, top));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, bodyBottom - top));
        // The ladder handles the mouse wheel itself (pan/zoom), so the pane mustn't scroll.
        ladderPreview = false;
        if (ImGui::Begin("Ladder", nullptr, paneFlags | ImGuiWindowFlags_NoScrollWithMouse)) {
            // What the ladder shows and edits: the sketch, or the pattern.
            const JugglingLoop ladderLoop = sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop());
            const LadderToolbarRequest request =
                drawLadderToolbar(ladderLoop, ladderEdit, settings.showThrowValues, settings.colorByOrbit,
                                  parsed.form.hasLinks(), settings.dimLinked);
            if (request.toggleOrbits) {
                settings.colorByOrbit = !settings.colorByOrbit;
                saveSettings(settings);
            }
            if (request.toggleCollapseAll) toggleCollapseAll(ladderEdit, ladderLoop.jugglers);
            if (request.toggleDimLinked) {
                settings.dimLinked = !settings.dimLinked;
                saveSettings(settings);
            }
            if (request.resetView) resetLadderView(ladderEdit);
            if (request.toggleValues) {
                settings.showThrowValues = !settings.showThrowValues;
                saveSettings(settings);
            }
            if (request.newPeriodBeats > 0 && patternCount >= 2 && request.newPeriodBeats % patternCount != 0) {
                siteswapBox.note = "Can't: this is a " + std::to_string(patternCount) + "-count, so the period goes up and down " +
                                   std::to_string(patternCount) + " beats at a time.";
                siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            } else if (request.newPeriodBeats > 0) {
                cancelLadderEdit(ladderEdit);
                commitTyping();
                const JugglingLoop longer = loopWithPeriod(ladderLoop, request.newPeriodBeats);
                // (Written as it is: a period chosen on purpose isn't shortened again.)
                std::string text;
                if (loopToText(longer, &text, &parsed.form)) {
                    const Siteswap check = parseSiteswap(text);
                    if (check.valid || check.sketch) {
                        std::snprintf(siteswapText, sizeof(siteswapText), "%s", text.c_str());
                        parsed = check;
                        patternValid = check.valid;
                        sketching = check.sketch;
                        if (check.valid) pattern = patternFromSiteswap(check);
                        if (check.sketch) sketchLoop = check.loop;
                        recordPattern(text);
                    }
                }
            }
            LadderViewOptions ladderOptions;
            ladderOptions.showValues = settings.showThrowValues;
            ladderOptions.colorByOrbit = settings.colorByOrbit;
            ladderOptions.selectedJuggler = selectedJuggler;
            ladderOptions.highlightThrow = siteswapHoverThrow;
            ladderOptions.relativeTargets = parsed.form.relative;
            ladderOptions.form = &parsed.form;
            ladderOptions.countN = patternCount;
            ladderOptions.writtenPeriod = played.expanded ? played.period : 0;
            ladderOptions.dimLinked = settings.dimLinked;
            ladderOptions.followPlayhead = playback.playing;
            {
                const JugglingLoop spinLoop = patternValid && !sketching ? played.loop : JugglingLoop();
                if (spinLoop != spinCacheLoop || juggleParams.bpm != spinCacheParams.bpm ||
                    juggleParams.dwellBeats != spinCacheParams.dwellBeats || juggleParams.prop != spinCacheParams.prop ||
                    juggleParams.distance != spinCacheParams.distance) {
                    spinCache = throwSpins(spinLoop, juggleParams);
                    spinCacheLoop = spinLoop;
                    spinCacheParams = juggleParams;
                }
                ladderOptions.spins = &spinCache;
            }
            {
                const JugglingLoop ladderLoop = shownLoop();
                const int written = choreographyCycle(ladderLoop, juggleParams);  // (the beats written out)
                ladderOptions.keyframeCycle = choreographyFullCycle(choreography, written);
                ladderOptions.showKeyframeMarks = settings.showKeyframeMarks;
                ladderOptions.walkLinks = choreographyMode && ladderLoop.jugglers > 1;
                const int cycle = ladderOptions.keyframeCycle;
                ladderOptions.walkLabels.assign(static_cast<size_t>(std::max(0, ladderLoop.jugglers)), std::string());
                for (int j = 0; j < ladderLoop.jugglers; ++j)
                    ladderOptions.walkLabels[static_cast<size_t>(j)] = walkLinkLabel(choreography.link(j));
                for (size_t j = 0; j < static_cast<size_t>(std::max(0, ladderLoop.jugglers)); ++j) {
                    const std::vector<Keyframe> keys = effectiveKeyframes(choreography, static_cast<int>(j), written);
                    const bool linked = choreography.link(static_cast<int>(j)).active();
                    for (size_t i = 0; i < keys.size(); ++i) {
                        const Keyframe& k = keys[i];
                        LadderKeyframe lk;
                        lk.juggler = static_cast<int>(j);
                        lk.beat = k.beat;
                        const bool onMark = k.mark >= 0 && k.mark < static_cast<int>(choreography.marks.size());
                        lk.where = onMark ? "on spike mark " + choreography.marks[static_cast<size_t>(k.mark)].name : "a turned spot";
                        lk.longTurn = k.longTurn;
                        lk.mark = onMark ? k.mark : -1;
                        if (onMark) lk.markName = choreography.marks[static_cast<size_t>(k.mark)].name;
                        lk.derived = linked || k.beat >= written;  // (a link's, or a rotated repeat's: locked)
                        if (cycle > 0) {
                            const Keyframe& next = keys[(i + 1) % keys.size()];
                            const int gap = keys.size() == 1 ? cycle : ((next.beat - k.beat) % cycle + cycle) % cycle;
                            if (gap > 0 && sameKeyframeSpot(choreography, k, next)) lk.stayBeats = gap;
                        }
                        ladderOptions.keyframes.push_back(lk);
                    }
                }
            }
            // (It shows the loop played out: what happens on each beat.)
            LadderEditResult edited = drawLadderDiagram(played.expanded ? played.loop : writtenLoop, settings.colorVision,
                                                        ladderEdit, playback.beat, ladderOptions);
            // Picking up a throw pauses the jugglers (don't confuse them mid-edit); drawing in a
            // sketch doesn't, so you can watch props pop in as you go.
            if (edited.startedChain) playback.playing = false;
            if (!edited.note.empty()) {
                siteswapBox.note = edited.note;
                siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            }
            ladderPreview = edited.hasPreview;
            if (ladderPreview) ladderPreviewLoop = edited.preview;
            if (edited.selectJuggler != kNoSelectionChange) {
                selectedJuggler = edited.selectJuggler;
                if (selectedJuggler < 0) frameSelected = false;
            }
            if (edited.committed) {
                const bool wasWorking = parsed.valid || parsed.sketch;
                // An edit to the loop played out goes into the beats written out (if that's where it was).
                JugglingLoop changed = edited.loop;
                std::string why;
                if (played.expanded && !writtenFromPlayed(played, writtenLoop, edited.loop, playPaths, &changed, &why)) {
                    siteswapBox.note = "Can't: " + why;
                    siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                    plipWanted = true;
                } else {
                    applyLoop(changed);
                    if (wasWorking && !parsed.valid && !parsed.sketch) plipWanted = true;  // (it broke it)
                }
            }
            if (edited.goToStart) goToStart();
            if (edited.walkLikeJuggler >= 0) openWalkLike(edited.walkLikeJuggler);
            if (edited.deleteKeyframeJuggler >= 0)
                removeKeyframe(&choreography, edited.deleteKeyframeJuggler, edited.deleteKeyframeBeat);
            // A keyframe's own changes: turning the long way round, or moved to another beat.
            auto keyframeAt = [&](int j, int beat) -> Keyframe* {
                if (j < 0 || j >= static_cast<int>(choreography.keys.size())) return nullptr;
                for (Keyframe& k : choreography.keys[static_cast<size_t>(j)])
                    if (k.beat == beat) return &k;
                return nullptr;
            };
            if (Keyframe* k = keyframeAt(edited.longTurnKeyframeJuggler, edited.longTurnKeyframeBeat)) k->longTurn = !k->longTurn;
            // With rotated repeats, keyframes stay in the beats written out.
            const int writtenBeats = choreographyCycle(shownLoop(), juggleParams);
            auto pastWritten = [&](int beat) {
                if (choreography.repeats() == 1 || beat < writtenBeats) return false;
                siteswapBox.note = "Can't: beat " + std::to_string(beat + 1) + " is in the walking loop's copies (only beats 1-" +
                                   std::to_string(writtenBeats) + " are written out; the C rows' next column says what comes after).";
                siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                return true;
            };
            if (edited.moveKeyframeJuggler >= 0 && pastWritten(edited.moveKeyframeTo)) edited.moveKeyframeJuggler = -1;
            if (edited.loiterJuggler >= 0 && pastWritten(edited.loiterTo)) edited.loiterJuggler = -1;
            if (Keyframe* k = keyframeAt(edited.moveKeyframeJuggler, edited.moveKeyframeFrom)) {
                Keyframe moved = *k;
                moved.beat = edited.moveKeyframeTo;
                removeKeyframe(&choreography, edited.moveKeyframeJuggler, edited.moveKeyframeFrom);
                setKeyframe(&choreography, edited.moveKeyframeJuggler, moved);  // (replacing one already there)
            }
            // Ctrl+dragged: a loiter, the keyframe copied to another beat. If the copy comes first,
            // the juggler arrives there (and any long way round goes with the arrival).
            if (Keyframe* k = keyframeAt(edited.loiterJuggler, edited.loiterFrom)) {
                Keyframe copy = *k;
                copy.beat = edited.loiterTo;
                copy.longTurn = edited.loiterBefore && k->longTurn;
                if (edited.loiterBefore) k->longTurn = false;
                setKeyframe(&choreography, edited.loiterJuggler, copy);
            }
            // Scrubbing in the beat numbers: the playhead follows the mouse, and playback stays
            // paused afterwards.
            if (edited.scrubbing) {
                playback.playing = false;
                playback.beat = edited.scrubBeat;
            }
            // A juggler's menu: Same as... (make them a link) or Unlink (write them out).
            if (edited.linkJuggler >= 0) linkJugglers(edited.linkJuggler, edited.linkTo, edited.linkStart);
            if (edited.unlinkJuggler >= 0) unlinkJugglers(edited.unlinkJuggler);
        }
        ImGui::End();

        // The siteswap grid (along the bottom, full width).
        ImGui::SetNextWindowPos(ImVec2(0.0f, bodyBottom));
        ImGui::SetNextWindowSize(ImVec2(W, entryHeight));
        siteswapHoverThrow = -1;
        if (ImGui::Begin("Siteswap entry", nullptr, paneFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ThrowLoopGridView throwView;
            throwView.active = writtenLoop.jugglers > 1;
            {
                const ThrowLoopPlan plan = throwLoopPlan(throwNext, writtenLoop.jugglers);
                for (int j = 0; j < writtenLoop.jugglers; ++j) {
                    const int k = j < static_cast<int>(throwNext.size()) ? throwNext[static_cast<size_t>(j)] : -1;
                    throwView.next.push_back(k >= 0 && k != j ? std::to_string(k + 1) : std::string());
                    throwView.bad.push_back(
                        static_cast<char>(std::find(plan.bad.begin(), plan.bad.end(), j) != plan.bad.end()));
                }
                if (played.expanded) throwView.played = played.loop;
                throwView.part = played.part;
                throwView.clash = played.clash;
            }
            ChoreoGridView choreoView = choreoGridView();
            // Who's on each row at the playhead (the rows are positions; see siteswap_grid.h).
            {
                const int now = static_cast<int>(std::floor(playback.beat));
                throwView.looping = played.part.size() > 1;
                for (int r = 0; r < writtenLoop.jugglers && throwView.looping; ++r) {
                    int who = r;
                    for (int j = 0; j < writtenLoop.jugglers; ++j)
                        if (played.partAt(j, now) == r) who = j;
                    throwView.who.push_back(who == r ? -1 : who);
                }
                choreoView.looping = !playPaths.trivial();
                for (int k = 0; k < static_cast<int>(choreoView.rows.size()) && choreoView.looping; ++k) {
                    const int who = playPaths.walker(k, now);
                    choreoView.who.push_back(who == k ? -1 : who);
                }
            }
            const SiteswapGridResult g = drawSiteswapGrid(siteswapGrid, siteswapText, parsed, patternCount,
                                                          settings.colorVision, playback.beat, choreoView, throwView);
            // The throws' next column: whose throws a juggler takes up after the period. (A change
            // that leaves it not working plips.)
            for (const std::pair<int, int>& e : g.throwNextEdits) {
                if (e.first < 0 || e.first >= kMaxJugglers) continue;
                if (static_cast<int>(throwNext.size()) <= e.first) throwNext.resize(static_cast<size_t>(e.first) + 1, -1);
                throwNext[static_cast<size_t>(e.first)] = e.second;
                const ThrowLoopPlan plan = throwLoopPlan(throwNext, writtenLoop.jugglers);
                if (!plan.valid || !playedLoop(writtenLoop, plan, playPaths).ok) plipWanted = true;
            }
            // The choreography's rows: keyframes typed, walk links, and the beat to show.
            // (Throws and walking changed together, a selection across both: one undo step.)
            const bool together = !g.choreoEdits.empty() && g.commit && g.newCount < 0;
            PatternSettings settingsBeforeBoth;
            if (together) {
                commitTyping();
                recordSettingsChange();
                settingsBeforeBoth = currentPatternSettings();
            }
            for (const ChoreoEdit& e : g.choreoEdits) {
                if (e.juggler < 0) continue;
                if (e.kind == ChoreoEdit::Kind::Set) {
                    Keyframe k;
                    k.beat = e.beat;
                    k.mark = findMarkNamed(choreography, e.mark);
                    if (k.mark < 0) continue;
                    if (e.longTurn >= 0)
                        k.longTurn = e.longTurn != 0;
                    else if (choreography.hasKeys(e.juggler))
                        for (const Keyframe& old : choreography.keys[static_cast<size_t>(e.juggler)])
                            if (old.beat == e.beat) k.longTurn = old.longTurn;
                    setKeyframe(&choreography, e.juggler, k);
                } else if (e.kind == ChoreoEdit::Kind::Rotate) {
                    // A beat (or more) later, round the beats written out.
                    const int written = std::max(1, choreographyCycle(shownLoop(), juggleParams));
                    if (choreography.hasKeys(e.juggler)) {
                        std::vector<Keyframe> keys = choreography.keys[static_cast<size_t>(e.juggler)];
                        choreography.keys[static_cast<size_t>(e.juggler)].clear();
                        for (Keyframe& k : keys) {
                            k.beat = ((k.beat + e.beat) % written + written) % written;
                            setKeyframe(&choreography, e.juggler, k);
                        }
                    }
                } else if (e.kind == ChoreoEdit::Kind::Clear) {
                    removeKeyframe(&choreography, e.juggler, e.beat);
                } else if (e.kind == ChoreoEdit::Kind::Loop) {
                    Continuation k;
                    if (!readContinuation(e.mark, &k)) {
                        siteswapBox.note = "\"" + e.mark + "\" isn't a row to go on to: a C row's number, then maybe a turn "
                                           "(2, 2r+1/4, r1/2).";
                        siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                    } else {
                        setContinuation(e.juggler, k);
                    }
                } else if (e.kind == ChoreoEdit::Kind::InsertBeat || e.kind == ChoreoEdit::Kind::DeleteBeat) {
                    for (int i = 0; i < std::max(1, e.count); ++i) walkingBeat(e.beat, e.kind == ChoreoEdit::Kind::InsertBeat);
                } else if (choreography.hasKeys(e.juggler)) {
                    for (Keyframe& k : choreography.keys[static_cast<size_t>(e.juggler)])
                        if (k.beat == e.beat) k.longTurn = !k.longTurn;
                }
            }
            if (g.walkLinkJuggler >= 0 && g.walkLinkLeader >= 0) {
                if (choreography.dependsOn(g.walkLinkLeader, g.walkLinkJuggler)) {
                    siteswapBox.note = "Can't: J" + std::to_string(g.walkLinkLeader + 1) + " already walks like J" +
                                       std::to_string(g.walkLinkJuggler + 1) + " (perhaps through others).";
                    siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                } else {
                    WalkLink l = choreography.link(g.walkLinkJuggler);
                    l.leader = g.walkLinkLeader;
                    l.offset = g.walkLinkOffset;
                    setWalkLink(&choreography, g.walkLinkJuggler, l);
                }
            }
            if (g.walkUnlinkJuggler >= 0) unlinkWalk(g.walkUnlinkJuggler);
            if (g.walkLikeJuggler >= 0) openWalkLike(g.walkLikeJuggler);
            if (g.loopBuilderJuggler >= 0) {
                loopBuilder.juggler = g.loopBuilderJuggler;
                loopBuilder.k = choreography.continuation(g.loopBuilderJuggler);
                loopBuilder.requested = true;
            }
            if (g.scrubBeat >= 0.0) {
                playback.playing = false;
                playback.beat = g.scrubBeat;
            }
            if (g.selectJuggler >= 0 && g.selectJuggler < shownLoop().jugglers && shownLoop().jugglers > 1)
                selectedJuggler = g.selectJuggler;
            if (g.newCount >= 0) {
                // A new count directive (and the throws moved to fit it, if asked): one undo step.
                commitTyping();
                recordSettingsChange();
                const PatternSettings before = currentPatternSettings();
                patternCount = g.newCount;
                if (g.changed) showPatternText(g.text);
                history.recordWithSettings(g.changed ? g.text : history.current, before, currentPatternSettings());
                settingsRecorded();
            } else if (together) {
                if (g.commitText != siteswapText) showPatternText(g.commitText);
                history.recordWithSettings(g.commitText, settingsBeforeBoth, currentPatternSettings());
                settingsRecorded();
            } else {
                if (g.commit) {
                    if (g.commitText != siteswapText) showPatternText(g.commitText);
                    recordPattern(g.commitText);
                }
                if (g.changed && g.text != siteswapText) showPatternText(g.text);
            }
            if (g.restart) playback.beat = 0.0;
            if (g.linkJuggler >= 0) linkJugglers(g.linkJuggler, g.linkTo, g.linkStart);
            if (g.unlinkJuggler >= 0) unlinkJugglers(g.unlinkJuggler);
            if (!g.note.empty()) {
                siteswapBox.note = g.note;
                siteswapBox.noteUntil = ImGui::GetTime() + 5.0;
            }
            if (g.plip) plipWanted = true;
            // Resting on a throw describes it and picks it out on the ladder.
            const Siteswap& shown = parsed;
            if (g.hoverJuggler >= 0 && played.expanded && g.hoverJuggler < played.loop.jugglers && g.hoverColumn >= 0) {
                // (The loop played out: the throw made then by whoever's on that row.)
                const int beat = g.hoverColumn % played.loop.period;
                int who = g.hoverJuggler;
                for (int j = 0; j < played.loop.jugglers; ++j)
                    if (played.partAt(j, beat) == g.hoverJuggler) who = j;
                siteswapHoverThrow = who * played.loop.period + beat;
                std::string says = describeThrow(played.loop, who, beat);
                if (played.part.size() > 1)
                    says += "\n(T" + std::to_string(g.hoverJuggler + 1) + "'s throw, made by J" + std::to_string(who + 1) + ")";
                ImGui::SetTooltip("%s", says.c_str());
            } else if (g.hoverJuggler >= 0 && !shown.loop.empty() && g.hoverJuggler < shown.loop.jugglers) {
                const int beat = g.hoverBeat % shown.loop.period;
                if (shown.valid || shown.sketch) siteswapHoverThrow = g.hoverJuggler * shown.loop.period + beat;
                ImGui::SetTooltip("%s", describeThrow(shown.loop, g.hoverJuggler, beat).c_str());
            }

            // The status line, beside the Count button.
            if (!siteswapBox.note.empty() && ImGui::GetTime() < siteswapBox.noteUntil)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)), "%s",
                                   siteswapBox.note.c_str());
            else if (!siteswapBox.info.empty() && ImGui::GetTime() < siteswapBox.infoUntil)
                ImGui::TextUnformatted(siteswapBox.info.c_str());
            else if (throwLoopBroken)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "The throwing loop doesn't work: %s (the stage shows it as it last worked).",
                                   throwLoopProblem.c_str());
            else if (walkLoopBroken)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "The walking loop doesn't work: %s (the stage shows it as it last worked).",
                                   walkLoopProblem.c_str());
            else if (patternValid && pattern.jugglers > 1)
                ImGui::TextDisabled("%d jugglers, %d objects, period %d", pattern.jugglers, ballCount(pattern),
                                    loopPeriodBeats(pattern));
            else if (patternValid)
                ImGui::TextDisabled("%d objects, period %d", ballCount(pattern), loopPeriodBeats(pattern));
            else if (sketching)
                ImGui::TextDisabled("Sketch: %d throw%s not decided yet, period %d", parsed.openThrows,
                                    parsed.openThrows == 1 ? "" : "s", sketchLoop.period);
            else if (!parsed.error.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "%s", parsed.error.c_str());
            else
                ImGui::TextUnformatted("");
            // Where the cursor is ("J1, beat 5, right hand: 3p+2"), at the right.
            if (!g.readout.empty()) {
                ImGui::SameLine();
                const float width = ImGui::CalcTextSize(g.readout.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
                ImGui::TextUnformatted(g.readout.c_str());
            }
        }
        ImGui::End();

        // --- The scene at this moment ---
        // The jugglers juggle the pattern, or the sketch as far as it goes (props pop in and
        // vanish where throws aren't decided yet), or, while drawing, what a click would make.
        const bool showJugglers = patternValid || sketching;
        const JugglingLoop loop = ladderPreview ? ladderPreviewLoop
                                                : (sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop()));
        // Choreography fixes the distance between the jugglers once anything in it has changed
        // (a mark moved, added or deleted, a keyframe): it puts them where they stand, in meters.
        // Until then it's just the default formation, kept up with the distance.
        // A choreography saved without a length gets one now (enough whole periods to hold its
        // keyframes), so it doesn't change when the siteswap does. (It's not an unsaved change.)
        if (!ladderPreview && !loop.empty() && choreography.active() && choreography.cycle == 0) {
            const std::string before = choreographyText();
            choreography.cycle = choreographyCycle(loop, juggleParams);
            if (savedChoreography == before) savedChoreography = choreographyText();
            if (untouchedChoreography == before) untouchedChoreography = choreographyText();
        }
        const bool choreographyLocked = choreography.active() && choreographyText() != untouchedChoreography;
        if (!ladderPreview && !loop.empty() && choreography.active()) {
            if (choreographyLocked) {
                if (loop.jugglers > 1 && choreography.reference > 0.0 && juggleParams.distance != choreography.reference)
                    juggleParams.distance = choreography.reference;
            } else if (static_cast<int>(choreography.marks.size()) != loop.jugglers || choreography.cycle != loop.period ||
                       std::fabs(choreography.reference - passingDistance(loop, juggleParams)) > 1e-9) {
                choreography = Choreography();
                ensureSpikeMarks();
            }
        }
        const bool loopComplete = !loop.empty() && openThrowCount(loop) == 0;
        if (selectedJuggler >= std::max(loop.jugglers, pattern.jugglers) || std::max(loop.jugglers, pattern.jugglers) < 2) {
            selectedJuggler = -1;
            frameSelected = false;
        }
        {
            const JugglingLoop shown = shownLoop();
            const WalkLoop w = choreography.active() && !shown.empty() ? walkLoop(choreography, shown.jugglers) : WalkLoop();
            walkLoopBroken = !w.valid;
            walkLoopProblem = w.problem;
            if (!walkLoopBroken) stageChoreography = choreography;
        }
        // What the stage is evaluated with: the choreography as it last worked.
        JuggleParams stageParams = juggleParams;
        stageParams.choreography = &stageChoreography;
        JugglerScene scene;
        std::vector<int> styleOfBall;  // with "color by orbit": each prop's orbit
        // ...playing the loop out (the throwing loop, as it last worked); or the ladder's preview.
        const JugglingLoop stageLoop = ladderPreview || loop.empty() ? loop : stagePlayLoop;
        if (showJugglers) {
            const BallOrbits ballOrbits = loopComplete ? computeBallOrbits(stageLoop) : BallOrbits();
            scene = evaluateScene(stageLoop, ballOrbits, stageParams, playback.beat);
            if (settings.colorByOrbit && loopComplete)
                styleOfBall = orbitOfEachBall(stageLoop, ballOrbits, computeLoopOrbits(stageLoop));
            const int framedJuggler = frameSelected ? selectedJuggler : -1;
            auto sameParams = [](const JuggleParams& a, const JuggleParams& b) {
                return a.bpm == b.bpm && a.dwellBeats == b.dwellBeats && a.prop == b.prop && a.distance == b.distance;
            };
            const double now = ImGui::GetTime();
            // (While a spike mark is dragged, the choreography changes every frame: the framing
            // waits till it settles, like for any other change.)
            const std::string choreographyNow = choreography.active() ? choreographyToText(choreography) : std::string();
            if (stageLoop != extentsSeenLoop || !sameParams(juggleParams, extentsSeenParams) ||
                choreographyNow != extentsSeenChoreography) {
                extentsSeenLoop = stageLoop;
                extentsSeenParams = juggleParams;
                extentsSeenChoreography = choreographyNow;
                extentsSeenSince = now;
            }
            const bool stale = stageLoop != extentsLoop || !sameParams(juggleParams, extentsParams) ||
                               framedJuggler != extentsJuggler || choreographyNow != extentsChoreography;
            // At once for the first framing and for framing a juggler (a click); otherwise once
            // the pattern has settled.
            // (Not while a juggler or mark is being moved, or the loiter popup is up: the view would
            // shift under the mouse.)
            const bool placing = jugglerDrag >= 0 || markDrag >= 0 || loiter.open;
            if (stale && !placing &&
                (!haveExtents || framedJuggler != extentsJuggler || now - extentsSeenSince >= kExtentsSettleSeconds)) {
                haveExtents = true;
                extents = computeSceneExtents(stageLoop, stageParams, framedJuggler);
                extentsLoop = stageLoop;
                extentsParams = juggleParams;
                extentsJuggler = framedJuggler;
                extentsChoreography = choreographyNow;
            }
        }

        // The stage a free camera orbits within: the jugglers, the spike marks and keyframe
        // spots, with a margin, from the floor to a bit above head height.
        auto stageBox = [&]() {
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
            const float scale = choreographyScale(loop);
            for (const SpikeMark& m : choreography.marks) include(m.x * scale, m.z * scale);
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, juggleParams);
            const int walkers = std::max(static_cast<int>(choreography.keys.size()), static_cast<int>(choreography.links.size()));
            for (int j = 0; j < walkers; ++j)
                for (const Keyframe& k : effectiveKeyframes(choreography, j, cycle)) {
                    float x = 0.0f, z = 0.0f, yaw = 0.0f;
                    keyframeSpot(choreography, k, &x, &z, &yaw);
                    include(x * scale, z * scale);
                }
            if (!any) return StageBox();
            box.min = Vec3(box.min.x - 1.5f, 0.0f, box.min.z - 1.5f);
            box.max = Vec3(box.max.x + 1.5f, 2.5f, box.max.z + 1.5f);
            return box;
        };
        // How far a point (the free camera) is from the nearest thing on stage: a juggler, a
        // prop or a spike mark. Dollying goes at a speed in proportion, so it's quick from far
        // away and fine up close.
        auto nearestToCamera = [&](Vec3 p) {
            float nearest = 1e9f;
            for (const JugglerState& js : scene.jugglers) {
                nearest = std::min(nearest, length(p - js.position));
                nearest = std::min(nearest, length(p - Vec3(js.position.x, 1.25f, js.position.z)));
            }
            for (const BallState& b : scene.balls) nearest = std::min(nearest, length(p - b.center));
            const float scale = choreographyScale(loop);
            for (const SpikeMark& m : choreography.marks)
                nearest = std::min(nearest, length(p - Vec3(m.x * scale, 0.0f, m.z * scale)));
            if (nearest >= 1e9f) nearest = 3.0f;
            return std::max(nearest, 0.3f);
        };

        // Choreography mode: the paths the jugglers walk.
        const bool showPaths = choreographyMode && choreography.active() && !loop.empty();
        const int pathLength = showPaths ? std::max(1, choreographyCycle(loop, juggleParams)) : 0;  // (written out)
        const int pathCycle = choreographyFullCycle(stageChoreography, pathLength);  // (the walking loop too)
        // With rotated repeats, only the beats written out are edited: the rest are them turned.
        // Whether `beat` falls in a rotated repeat (and if so, a note saying so).
        auto inRotatedRepeat = [&](long beat) {
            if (loop.empty() || choreography.repeats() == 1) return false;
            const int written = std::max(1, choreographyCycle(loop, juggleParams));
            const int whole = choreographyFullCycle(choreography, written);
            const int b = static_cast<int>(((beat % whole) + whole) % whole);
            if (b < written) return false;
            siteswapBox.note = "Beat " + std::to_string(b + 1) + " is in the walking loop's copies (beats " + std::to_string(written + 1) +
                               "-" + std::to_string(whole) + " come from beats 1-" + std::to_string(written) +
                               " by the C rows' next column): place jugglers on beats 1-" + std::to_string(written) + " instead.";
            siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
            return true;
        };
        const ChoreographyPaths paths =
            showPaths ? choreographyPaths(stageChoreography, loop.jugglers, pathLength, choreographyScale(loop)) : ChoreographyPaths();
        int hoveredPathPoint = -1;

        // Mouse input for the 3D view: an invisible window over the juggler pane (the 3D view
        // itself is drawn straight to OpenGL, not by ImGui). Right-drag to orbit, wheel to zoom; with
        // passing patterns, click a juggler to select them (click empty space to deselect) and
        // double-click to frame them (double-click empty space to frame everyone).
        const float transportHeightForInput = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        const ImVec2 viewMin(leftWidth, top);
        const ImVec2 viewSize(W - leftWidth, bodyBottom - top - transportHeightForInput);
        ImGui::SetNextWindowPos(viewMin);
        ImGui::SetNextWindowSize(viewSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Juggler view input", nullptr,
                         paneFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse)) {
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
                    viewPress = -1;
                    pathPress = -1;
                }
                if (ImGui::IsItemActivated())
                    viewPress = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? 0 : (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? 1 : 2);
                const bool leftPress = viewPress == 0;
                const float clickSlop = io.MouseDragThreshold;
                const bool dragged =
                    io.MouseDragMaxDistanceSqr[std::max(0, viewPress)] >= clickSlop * clickSlop;
                // Choreography: the spike marks can be dragged (moved, or turned by the arrow) and
                // clicked (a keyframe there for the selected juggler, on the current beat).
                const bool editingFloor = choreographyMode && choreography.active();
                const float markScale = choreographyScale(loop);
                const bool canSelect = scene.jugglers.size() > 1;
                if (ImGui::IsItemActivated()) {
                    viewPan = viewPress == 2 || (viewPress == 1 && io.KeyShift);
                    viewOrbitLeft = viewPress == 0 && io.KeyAlt;
                }
                // A path point under the mouse (a click on it goes to its beat, with its juggler
                // selected).
                if (showPaths && ImGui::IsItemHovered() && markDrag < 0 && jugglerDrag < 0)
                    hoveredPathPoint = pickPathPoint(paths, lastViewProj, viewMin, viewSize, io.MousePos,
                                                     BodyOccluder(scene, lastCamera.position));
                if (ImGui::IsItemActivated() && leftPress && !pressTakenByChoice) pathPress = hoveredPathPoint;
                const int underMouse =
                    canSelect && ImGui::IsItemHovered() ? pickJuggler(scene, lastViewProj, viewMin, viewSize, io.MousePos)
                                                        : -1;
                // Any part of a mark can be dragged: anywhere inside its ring moves it, and its
                // arrow turns it. A click on it puts the selected juggler there. Someone standing on
                // it is picked instead only where the mouse is squarely on their body (no slack),
                // so the floor showing inside the ring is the mark's. The arrow beats everything.
                bool onArrow = false;
                Vec3 floorHere(0.0f, 0.0f, 0.0f);
                const bool haveFloor =
                    editingFloor && floorUnderMouse(lastCamera, lastAspect, viewMin, viewSize, io.MousePos, &floorHere);
                // (A mark's name comes first: it picks that mark, even among several on one spot.)
                const int labelUnderMouse = editingFloor && ImGui::IsItemHovered() && markDrag < 0
                                                ? markLabelUnder(choreography, markScale, lastViewProj, viewMin, viewSize, io.MousePos,
                                                                 markGroupView().badgeH())
                                                : -1;
                const int markUnderMouse = labelUnderMouse >= 0 ? labelUnderMouse
                                           : editingFloor && ImGui::IsItemHovered() && markDrag < 0
                                               ? pickSpikeMark(choreography, markScale, lastViewProj, viewMin, viewSize,
                                                               io.MousePos, haveFloor, floorHere, &onArrow)
                                               : -1;
                const bool onBody = labelUnderMouse < 0 && underMouse >= 0 && markUnderMouse >= 0 && !onArrow &&
                                    pickJuggler(scene, lastViewProj, viewMin, viewSize, io.MousePos, 0.0f) >= 0;
                const bool markRing = markUnderMouse >= 0 && (onArrow || !onBody);
                // A mark (or its arrow) comes before the path points drawn on it (a juggler standing
                // on a mark has a path point in its middle).
                if (markRing) {
                    hoveredPathPoint = -1;
                    if (ImGui::IsItemActivated()) pathPress = -1;
                }
                // Choosing among marks in the same place: a click on an arrow's tip or its number picks
                // that mark; a click anywhere else keeps the one they're on for now.
                if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) pressTakenByChoice = false;
                if (markChoice.active && ImGui::IsItemActivated() && leftPress) {
                    pressTakenByChoice = true;
                    std::vector<bool> shown;
                    const std::vector<ImVec2> bubbles =
                        markBubbleCenters(choreography, markScale, lastViewProj, viewMin, viewSize, &shown, markGroupView().badgeH());
                    int best = -1;
                    float bestD = ImGui::GetFontSize() * 1.4f;
                    for (const int m : markChoice.marks) {
                        if (m < 0 || m >= static_cast<int>(choreography.marks.size())) continue;
                        Vec3 middle, tip;
                        markGeometry(choreography.marks[static_cast<size_t>(m)], markScale, &middle, &tip);
                        ImVec2 t;
                        if (projectToScreen(lastViewProj, tip, viewMin, viewSize, &t)) {
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
                        Keyframe k = markChoice.pending;
                        k.mark = best;
                        setKeyframe(&choreography, markChoice.juggler, k);
                        openLoiterPopup(markChoice.juggler, k, markChoice.at);
                    }
                    markChoice.active = false;
                }
                if (editingFloor && ImGui::IsItemActivated() && leftPress && !pressTakenByChoice) {
                    if (markUnderMouse >= 0 && (underMouse < 0 || markRing)) {
                        markDrag = markUnderMouse;
                        markTurning = onArrow;
                        markForKeyframe = markRing;
                        // Pressed: it's in the selection now (on its own, or added with Shift), so
                        // a drag moves the selection.
                        markPressWasSelected =
                            std::find(selectedMarks.begin(), selectedMarks.end(), markDrag) != selectedMarks.end();
                        if (!markPressWasSelected) {
                            if (!io.KeyShift) selectedMarks.clear();
                            selectedMarks.push_back(markDrag);
                        }
                        markDragStart.clear();
                        for (const int m : selectedMarks)
                            markDragStart.push_back({choreography.marks[static_cast<size_t>(m)].x,
                                                     choreography.marks[static_cast<size_t>(m)].z});
                    } else if (underMouse >= 0 && !loop.empty() && choreography.link(underMouse).active()) {
                        // A juggler who walks like another goes where the leader goes.
                        siteswapBox.note = "J" + std::to_string(underMouse + 1) + " " + walkLinkLabel(choreography.link(underMouse)) +
                                           ": move J" + std::to_string(choreography.link(underMouse).leader + 1) +
                                           ", or unlink J" + std::to_string(underMouse + 1) + " (right-click them: Walk Like...).";
                        siteswapBox.noteUntil = ImGui::GetTime() + 5.0;
                    } else if (underMouse >= 0 && !loop.empty() && inRotatedRepeat(std::lround(playback.beat))) {
                        // (Not in a rotated repeat: the note says where to do it.)
                    } else if (underMouse >= 0 && !loop.empty()) {
                        // A juggler: dragging them puts them somewhere new (a keyframe).
                        jugglerDrag = underMouse;
                        Vec3 at;
                        jugglerPlacementAt(loop, juggleParams, underMouse, std::round(playback.beat), &at, &jugglerDragYaw);
                    }
                }
                if (jugglerDrag >= 0 && !ImGui::IsItemActive()) {
                    // Dropped: how long do they stay?
                    if (ImGui::IsItemDeactivated() && dragged && jugglerDrag < loop.jugglers && !loop.empty()) {
                        const int cycle = std::max(1, choreographyCycle(loop, juggleParams));
                        const long beat = std::lround(playback.beat);
                        const int b = static_cast<int>(((beat % cycle) + cycle) % cycle);
                        Keyframe placed;
                        bool found = false;
                        if (jugglerDrag < static_cast<int>(choreography.keys.size()))
                            for (const Keyframe& k : choreography.keys[static_cast<size_t>(jugglerDrag)])
                                if (k.beat == b) {
                                    placed = k;
                                    found = true;
                                }
                        // Dropped off every mark: a new mark there, which the keyframe is on (every
                        // keyframe is on a mark).
                        if (found && placed.mark < 0) {
                            placed.mark = markForSpot(&choreography, placed.x, placed.z, placed.yaw);
                            setKeyframe(&choreography, jugglerDrag, placed);
                        }
                        Vec3 floor;
                        const std::vector<int> here =
                            found && placed.mark >= 0 && floorUnderMouse(lastCamera, lastAspect, viewMin, viewSize, io.MousePos, &floor)
                                ? marksAround(floor.x, floor.z, markScale)
                                : std::vector<int>();
                        if (here.size() >= 2) {
                            markChoice = MarkChoice{true, jugglerDrag, placed, here, io.MousePos};
                        } else if (found) {
                            openLoiterPopup(jugglerDrag, placed, io.MousePos);
                        }
                    }
                    jugglerDrag = -1;
                }
                if (jugglerDrag >= 0 && jugglerDrag < loop.jugglers && dragged) {
                    // Dragged to the floor under the mouse, on the current beat: onto a spike mark
                    // if it's dropped on one (facing as the mark says), else a spot (snapped to the
                    // floor grid; Shift: freely), facing as they were, which gets a mark of its own
                    // when they're let go.
                    Vec3 floor;
                    if (floorUnderMouse(lastCamera, lastAspect, viewMin, viewSize, io.MousePos, &floor)) {
                        const int cycle = std::max(1, choreographyCycle(loop, juggleParams));
                        const long beat = std::lround(playback.beat);
                        Keyframe k;
                        k.beat = static_cast<int>(((beat % cycle) + cycle) % cycle);
                        for (const Keyframe& old : choreography.keys.size() > static_cast<size_t>(jugglerDrag)
                                                       ? choreography.keys[static_cast<size_t>(jugglerDrag)]
                                                       : std::vector<Keyframe>())
                            if (old.beat == k.beat) k.longTurn = old.longTurn;
                        for (int m = 0; m < static_cast<int>(choreography.marks.size()); ++m) {
                            const SpikeMark& sm = choreography.marks[static_cast<size_t>(m)];
                            if (std::hypot(floor.x - sm.x * markScale, floor.z - sm.z * markScale) <= kMarkRadius * markScale)
                                k.mark = m;
                        }
                        if (k.mark < 0) {
                            float x = floor.x, z = floor.z;
                            if (!io.KeyShift) snapToFloorGrid(settings, &x, &z);
                            k.x = x / markScale;
                            k.z = z / markScale;
                            k.yaw = jugglerDragYaw;
                        }
                        setKeyframe(&choreography, jugglerDrag, k);
                        playback.playing = false;
                        playback.beat = static_cast<double>(beat);
                        selectedJuggler = jugglerDrag;
                    }
                }
                if (markDrag >= 0 && markDrag < static_cast<int>(choreography.marks.size()) && ImGui::IsItemActive() &&
                    dragged) {
                    Vec3 floor;
                    if (floorUnderMouse(lastCamera, lastAspect, viewMin, viewSize, io.MousePos, &floor)) {
                        SpikeMark& m = choreography.marks[static_cast<size_t>(markDrag)];
                        if (markTurning) {
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
                        } else {
                            // Moved to the mouse: snapped to the floor grid (Shift: freely); the rest
                            // of the selection moves with it, keeping their shape.
                            float x = floor.x, z = floor.z;
                            if (!io.KeyShift) snapToFloorGrid(settings, &x, &z);
                            const auto here = std::find(selectedMarks.begin(), selectedMarks.end(), markDrag);
                            const size_t me = static_cast<size_t>(here - selectedMarks.begin());
                            if (here == selectedMarks.end() || markDragStart.size() != selectedMarks.size()) {
                                m.x = x / markScale;
                                m.z = z / markScale;
                            } else {
                                const float dx = x / markScale - markDragStart[me].first;
                                const float dz = z / markScale - markDragStart[me].second;
                                for (size_t i = 0; i < selectedMarks.size(); ++i) {
                                    const int k = selectedMarks[i];
                                    if (k < 0 || k >= static_cast<int>(choreography.marks.size())) continue;
                                    choreography.marks[static_cast<size_t>(k)].x = markDragStart[i].first + dx;
                                    choreography.marks[static_cast<size_t>(k)].z = markDragStart[i].second + dz;
                                }
                            }
                        }
                    }
                }
                const bool cameraDrag = ImGui::IsItemActive() && markDrag < 0 && jugglerDrag < 0 && viewPress >= 0 &&
                                        (viewPress != 0 || viewOrbitLeft) &&
                                        ImGui::IsMouseDragging(static_cast<ImGuiMouseButton>(viewPress), 0.0f);
                const bool orbiting = cameraDrag && !viewPan;
                const bool panning = cameraDrag && viewPan;
                if (orbiting) {
                    const ImVec2 d = io.MouseDelta;
                    // In the top view, orbiting turns the view round the vertical (it stays
                    // looking straight down).
                    if (camera.free) {
                        if (!cameraOrbiting) orbitCenter = freeOrbitCenter(lastCamera, scene, selectedJuggler, stageBox());
                        orbitFreeCamera(camera, orbitCenter, -d.x * 0.008f, d.y * 0.008f);
                    } else {
                        camera.yaw -= d.x * 0.008f;
                        if (!camera.topView) camera.pitch = std::clamp(camera.pitch + d.y * 0.008f, -1.45f, 1.45f);
                    }
                }
                if (panning) {
                    // Panning drags the floor under the mouse along with it (and frees the camera).
                    if (!camera.free) makeCameraFree(camera, lastCamera);
                    if (!cameraPanning) {
                        Vec3 floor;
                        panDepth = floorUnderMouse(lastCamera, lastAspect, viewMin, viewSize, io.MousePos, &floor)
                                       ? std::min(length(floor - lastCamera.position), 40.0f)
                                       : length(freeOrbitCenter(lastCamera, scene, selectedJuggler, stageBox()) - lastCamera.position);
                    }
                    const float metersPerPixel = 2.0f * panDepth * std::tan(kCameraFovY * 0.5f) / std::max(1.0f, viewSize.y);
                    Vec3 forward, right, up;
                    freeCameraAxes(camera, &forward, &right, &up);
                    camera.freePos = camera.freePos - right * (io.MouseDelta.x * metersPerPixel) +
                                     up * (io.MouseDelta.y * metersPerPixel);
                }
                cameraOrbiting = orbiting;
                cameraPanning = panning;
                // A click is a press and release without dragging (dragging orbits).
                if (pathPress >= 0 && pathPress < static_cast<int>(paths.points.size()) && ImGui::IsItemDeactivated() &&
                    leftPress && !dragged) {
                    // A click on a path point: its beat (in the cycle playing now), its juggler selected.
                    const PathPoint& point = paths.points[static_cast<size_t>(pathPress)];
                    if (loop.jugglers > 1) selectedJuggler = point.juggler;
                    playback.playing = false;
                    playback.beat = std::floor(playback.beat / pathCycle) * pathCycle + point.firstBeat;
                    markDrag = -1;
                    jugglerDrag = -1;
                    pathPress = -1;
                } else if (markDrag >= 0 && ImGui::IsItemDeactivated() && leftPress && !dragged && !markForKeyframe) {
                    selectedJuggler = underMouse;  // a click on someone standing on a mark
                    markDrag = -1;
                } else if (markDrag >= 0 && ImGui::IsItemDeactivated()) {
                    if (!dragged) {
                        // A click on a mark selects it (Shift: adds it, or takes it away if it was
                        // already in), and no juggler. (A keyframe: drag a juggler onto it.)
                        if (io.KeyShift) {
                            if (markPressWasSelected)
                                selectedMarks.erase(std::remove(selectedMarks.begin(), selectedMarks.end(), markDrag),
                                                    selectedMarks.end());
                        } else {
                            selectedMarks.assign(1, markDrag);
                        }
                        selectedJuggler = -1;
                        frameSelected = false;
                    }
                    markDrag = -1;
                } else if (canSelect && ImGui::IsItemDeactivated() && leftPress && !dragged && !pressTakenByChoice) {
                    selectedJuggler = underMouse;
                    selectedMarks.clear();
                    if (underMouse < 0) frameSelected = false;
                }
                // Right-click (without dragging) the floor: add a mark there, or (on a mark) its menu.
                if (editingFloor && ImGui::IsItemDeactivated() && viewPress == 1 && !dragged) {
                    markMenuMark = markRing ? markUnderMouse : -1;
                    Vec3 floor(0.0f, 0.0f, 0.0f);
                    if (markMenuMark >= 0 || underMouse >= 0 ||
                        floorUnderMouse(lastCamera, lastAspect, viewMin, viewSize, io.MousePos, &floor)) {
                        markMenuPoint = Vec3(floor.x / markScale, 0.0f, floor.z / markScale);
                        floorMenuJuggler = underMouse;
                        ImGui::OpenPopup("##floor_menu");
                    }
                }
                if (ImGui::BeginPopup("##floor_menu")) {
                    if (floorMenuJuggler >= 0 && floorMenuJuggler < loop.jugglers && loop.jugglers > 1) {
                        const std::string how = walkLinkLabel(choreography.link(floorMenuJuggler));
                        ImGui::TextDisabled("Juggler %d%s%s", floorMenuJuggler + 1, how.empty() ? "" : ": ", how.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Walk Like...")) openWalkLike(floorMenuJuggler);
                        ImGui::Separator();
                    }
                    if (markMenuMark >= 0 && markMenuMark < static_cast<int>(choreography.marks.size())) {
                        ImGui::TextDisabled("Spike mark %s", choreography.marks[static_cast<size_t>(markMenuMark)].name.c_str());
                        ImGui::Separator();
                        if (ImGui::MenuItem("Face the Middle")) {
                            SpikeMark& m = choreography.marks[static_cast<size_t>(markMenuMark)];
                            if (std::hypot(m.x, m.z) > 1e-3f) m.yaw = std::atan2(-m.x, -m.z);
                        }
                        if (ImGui::MenuItem("Rename...")) {
                            renameMark.mark = markMenuMark;
                            renameMark.requested = true;
                        }
                        if (ImGui::MenuItem("Delete Spike Mark")) {
                            std::string why;
                            if (!removeSpikeMark(&choreography, markMenuMark, loop.empty() ? 0 : loop.jugglers, &why)) {
                                siteswapBox.note = why;
                                siteswapBox.noteUntil = ImGui::GetTime() + 6.0;
                            } else {
                                selectedMarks.clear();
                            }
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Only a mark nobody stands on (the others keep their names).");
                        if (ImGui::MenuItem("Add Another Spike Mark Here")) {
                            // Same place, facing the same way for now (turn it by its arrow).
                            SpikeMark copy = choreography.marks[static_cast<size_t>(markMenuMark)];
                            copy.name.clear();
                            addSpikeMark(&choreography, copy);
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("A second mark on the same spot (say, facing another way: drag its arrow).\n"
                                              "Putting a juggler there then asks which mark they stand on.");
                    } else if (floorMenuJuggler < 0) {
                        if (ImGui::MenuItem("Add Spike Mark Here")) {
                            SpikeMark m;
                            float x = markMenuPoint.x * markScale, z = markMenuPoint.z * markScale;
                            snapToFloorGrid(settings, &x, &z);
                            m.x = x / markScale;
                            m.z = z / markScale;
                            m.yaw = std::hypot(m.x, m.z) > 1e-3f ? std::atan2(-m.x, -m.z) : 0.0f;  // facing the middle
                            addSpikeMark(&choreography, m);
                        }
                    }
                    ImGui::EndPopup();
                }
                // Things that can be dragged show it: a hand over a juggler (in choreography).
                if (editingFloor && jugglerDrag >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                else if (editingFloor && underMouse >= 0 && markDrag < 0 && !orbiting) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (editingFloor && (markUnderMouse >= 0 || markDrag >= 0))
                    ImGui::SetMouseCursor(markTurning && markDrag >= 0 ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
                if (hoveredPathPoint >= 0 && !ImGui::IsItemActive()) {
                    const PathPoint& point = paths.points[static_cast<size_t>(hoveredPathPoint)];
                    const std::string beats = pathPointBeats(point, pathCycle);
                    ImGui::SetTooltip("J%d, beat%s %s\nClick to go to %s, J%d selected", point.juggler + 1,
                                      point.beats > 1 ? "s" : "", beats.c_str(),
                                      point.beats > 1 ? "its first beat" : "that beat", point.juggler + 1);
                } else if (editingFloor && markUnderMouse >= 0 && !ImGui::IsItemActive() && (markRing || onArrow)) {
                    if (onArrow)
                        ImGui::SetTooltip("Spike mark %s: drag the arrow to turn it (Shift: freely)",
                                          choreography.marks[static_cast<size_t>(markUnderMouse)].name.c_str());
                    else
                        ImGui::SetTooltip("Spike mark %s: drag to move it (it snaps to the grid; Shift: freely)\n"
                                          "Click to select it, Shift+click to add it to the selection: dragging one\n"
                                          "moves them all. To put a juggler here, drag them onto it.\n"
                                          "Right-click for more",
                                          choreography.marks[static_cast<size_t>(markUnderMouse)].name.c_str());
                }
                if (canSelect && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selectedJuggler = underMouse;
                    frameSelected = underMouse >= 0;
                    camera.free = false;
                }
                if (ImGui::IsItemHovered()) {
                    if (io.MouseWheel != 0.0f && camera.free) {
                        // A free camera moves along its view, more slowly the nearer it is to
                        // something.
                        Vec3 forward, right, up;
                        freeCameraAxes(camera, &forward, &right, &up);
                        camera.freePos = camera.freePos + forward * (io.MouseWheel * 0.15f * nearestToCamera(camera.freePos));
                    } else if (io.MouseWheel != 0.0f) {
                        camera.zoom = std::clamp(camera.zoom * std::pow(0.88f, io.MouseWheel), 0.15f, 6.0f);
                    }
                    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home)) {
                        resetCamera(camera);
                        frameSelected = false;
                    }
                    // Paused, resting on a club or ring in the air: its throw and spin rate.
                    bool propTip = false;
                    if (!playback.playing && scene.prop != PropType::Ball && !ImGui::IsItemActive() && hoveredPathPoint < 0) {
                        const BallState* nearest = nullptr;
                        float nearestD = 22.0f;  // pixels
                        for (const BallState& b : scene.balls) {
                            ImVec2 at;
                            if (!b.inFlight || b.flightBeats <= 0.0f ||
                                !projectToScreen(lastViewProj, b.center, viewMin, viewSize, &at))
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
                    if (!propTip && hoveredPathPoint < 0 && !ImGui::IsItemActive() && (markUnderMouse < 0 || underMouse >= 0) &&
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

        // The divider between the ladder and juggler panes: drag to resize them, double-click
        // to share the width equally again. (A thin window of its own over the boundary, so it
        // gets the mouse before either pane.)
        {
            const float grab = 4.0f;  // half the width that catches the mouse
            ImGui::SetNextWindowPos(ImVec2(leftWidth - grab, top));
            ImGui::SetNextWindowSize(ImVec2(grab * 2.0f, bodyBottom - top));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::Begin("##divider", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::InvisibleButton("##split", ImVec2(grab * 2.0f, bodyBottom - top));
                const bool hovered = ImGui::IsItemHovered();
                const bool active = ImGui::IsItemActive();
                if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                    settings.ladderFraction = std::clamp(io.MousePos.x / W, 0.1f, 0.9f);
                if (ImGui::IsItemDeactivated()) saveSettings(settings);
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    settings.ladderFraction = 0.5f;
                    saveSettings(settings);
                }
                if (hovered || active)
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(leftWidth, top), ImVec2(leftWidth, bodyBottom),
                                                        ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive
                                                                                  : ImGuiCol_SeparatorHovered),
                                                        2.0f);
                if (hovered && !active && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Drag to resize the panes; double-click to share the width equally");
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
        }

        // Tweakables, in the top-right corner of the juggler pane (over the input window, which
        // never comes to the front).
        const float panelMargin = style.WindowPadding.x;
        float tweakablesBottom = top + panelMargin;
        if (drawTweakablesPanel(juggleParams, loop, settings.tempoPanelCollapsed,
                                ImVec2(W - panelMargin, top + panelMargin), tweakablesPanel, choreographyLocked,
                                &tweakablesBottom))
            saveSettings(settings);
        groupChipHovered = -1;  // (set again below while the panel is up)
        if (choreographyMode && choreography.active() && !loop.empty()) {
            int newLength = -1;

            const std::string gridBefore = floorGridText(settings);
            choreographyPanel.groupSizes.assign(Choreography::kMarkGroups, 0);
            for (int g = 0; g < Choreography::kMarkGroups; ++g)
                choreographyPanel.groupSizes[static_cast<size_t>(g)] = static_cast<int>(choreography.group(g).size());
            if (drawChoreographyPanel(settings, ImVec2(W - panelMargin, tweakablesBottom + panelMargin * 0.5f),
                                      choreographyPanel, choreographyCycle(loop, juggleParams), &newLength,
                                      choreography.repeats(), loop.period))
                saveSettings(settings);
            groupChipHovered = choreographyPanel.hoveredGroup;
            if (choreographyPanel.clickedGroup >= 0) selectMarkGroup(choreographyPanel.clickedGroup, false);
            if (choreographyPanel.addedGroup >= 0) selectMarkGroup(choreographyPanel.addedGroup, true);
            if (choreographyPanel.emptiedGroup >= 0) setMarkGroup(choreographyPanel.emptiedGroup, std::vector<int>());
            // The grid changed: it's the choreography's (saved with the pattern). (A choreography
            // nobody has changed yet stays that way: the grid doesn't fix the passing distance.)
            if (floorGridText(settings) != gridBefore) {
                const bool untouched = choreographyText() == untouchedChoreography;
                choreography.grid = floorGridText(settings);
                choreographyGridShown = choreography.grid;
                if (untouched) untouchedChoreography = choreographyText();
            }
            if (newLength > 0) {
                // Shorter than a keyframe's beat: ask first (those keyframes go).
                bool strands = false;
                for (const std::vector<Keyframe>& keys : choreography.keys)
                    for (const Keyframe& k : keys) strands = strands || k.beat >= newLength;
                if (strands) {
                    pendingChoreographyLength = newLength;
                    ImGui::OpenPopup("Shorten the choreography?");
                } else {
                    choreography.cycle = newLength;
                }
            }
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Shorten the choreography?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            std::string gone;
            int count = 0;
            for (size_t j = 0; j < choreography.keys.size(); ++j)
                for (const Keyframe& k : choreography.keys[j])
                    if (k.beat >= pendingChoreographyLength) {
                        gone += (count++ == 0 ? "" : ", ") + std::string("J") + std::to_string(j + 1) + " on beat " +
                                std::to_string(k.beat + 1);
                    }
            ImGui::PushTextWrapPos(em * 30.0f);
            ImGui::Text("A %d-beat choreography has no room for %s %s. Delete %s?", pendingChoreographyLength,
                        count == 1 ? "this keyframe:" : "these keyframes:", gone.c_str(), count == 1 ? "it" : "them");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button("Shorten", ImVec2(em * 7.0f, 0.0f))) {
                for (std::vector<Keyframe>& keys : choreography.keys)
                    keys.erase(std::remove_if(keys.begin(), keys.end(),
                                              [&](const Keyframe& k) { return k.beat >= pendingChoreographyLength; }),
                               keys.end());
                choreography.cycle = pendingChoreographyLength;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Warnings, in the juggler pane under the pattern's name, until dismissed. A dismissed
        // warning comes back if it goes away and something brings it back (its key includes
        // what it's about).
        {
            std::vector<std::pair<std::string, std::string>> warnings;  // key, text
            if (choreography.active() && !loop.empty() && !ladderPreview) {
                const int cycle = choreographyFullCycle(choreography, choreographyCycle(loop, juggleParams));
                const int period = loop.period;
                if (cycle > 0 && period > 0 && cycle % period != 0 && period % cycle != 0) {
                    long a = cycle, b = period;
                    while (b != 0) {
                        const long r = a % b;
                        a = b;
                        b = r;
                    }
                    const long together = static_cast<long>(cycle) / a * period;
                    warnings.emplace_back("lengths:" + std::to_string(period) + ":" + std::to_string(cycle),
                                          "The pattern repeats every " + std::to_string(period) +
                                              " beats and the choreography every " + std::to_string(cycle) +
                                              ", so they only line up again every " + std::to_string(together) +
                                              " beats.");
                }
            }
            for (std::set<std::string>::iterator it = dismissedWarnings.begin(); it != dismissedWarnings.end();) {
                bool stillThere = false;
                for (const std::pair<std::string, std::string>& w : warnings) stillThere = stillThere || w.first == *it;
                it = stillThere ? std::next(it) : dismissedWarnings.erase(it);
            }
            std::vector<std::pair<std::string, std::string>> shown;
            for (const std::pair<std::string, std::string>& w : warnings)
                if (dismissedWarnings.count(w.first) == 0) shown.push_back(w);
            if (!shown.empty()) {
                const float em = ImGui::GetFontSize();
                ImGui::SetNextWindowPos(ImVec2(viewMin.x + panelMargin, viewMin.y + panelMargin + kTitleFontSize + em * 0.6f));
                ImGui::SetNextWindowBgAlpha(0.88f);
                const ImU32 warn = errorTextColor(settings.colorVision);
                ImGui::PushStyleColor(ImGuiCol_Border, warn);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
                if (ImGui::Begin("##warnings", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
                    for (const std::pair<std::string, std::string>& w : shown) {
                        ImGui::PushID(w.first.c_str());
                        ImGui::PushFont(titleFont, kTitleFontSize);
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(warn), "!");
                        ImGui::PopFont();
                        ImGui::SameLine();
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + em * 24.0f);
                        ImGui::TextUnformatted(w.second.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Dismiss")) dismissedWarnings.insert(w.first);
                        ImGui::PopID();
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
        }

        // Transport bar along the bottom of the juggler pane (right half, above the siteswap entry).
        const float transportHeight = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(leftWidth, bodyBottom - transportHeight));
        ImGui::SetNextWindowSize(ImVec2(W - leftWidth, transportHeight));
        if (ImGui::Begin("Transport", nullptr, paneFlags)) {
            const float h = ImGui::GetFrameHeight();
            if (transportButton("##tostart", TransportIcon::ToStart, h)) goToStart();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to beat 1 (B)");
            ImGui::SameLine();
            if (transportButton("##stepback", TransportIcon::StepBack, h))
                stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step back 1/12 beat (Left)\nShift: one beat (Shift+Left)");
            ImGui::SameLine();
            if (transportButton("##playpause", playback.playing ? TransportIcon::Pause : TransportIcon::Play, h))
                playback.playing = !playback.playing;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(playback.playing ? "Pause (Space)" : "Play (Space)");
            ImGui::SameLine();
            if (transportButton("##stepforward", TransportIcon::StepForward, h))
                stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step forward 1/12 beat (Right)\nShift: one beat (Shift+Right)");
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
            // The beat (beat 0 is "beat 1", as on the ladder): drag it to scrub, or double-click
            // (or Ctrl+click) it to type a beat to go to. Either pauses, as stepping does.
            {
                double shown = playback.beat + 1.0;
                ImGui::SetNextItemWidth(h * 5.0f);
                if (ImGui::DragScalar("##beat", ImGuiDataType_Double, &shown, 0.02f, nullptr, nullptr, "Beat %.2f")) {
                    playback.beat = shown - 1.0;
                    playback.playing = false;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Drag to move through time; double-click to type a beat.\n"
                                      "On the ladder, click or drag in the beat numbers.");
            }
            // Slow motion: on/off, and how slow.
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
            {
                const bool slow = playback.slow;
                if (slow) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button("Slow")) playback.slow = !playback.slow;
                if (slow) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow motion (S), at the speed set beside it");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(h * 4.5f);
                if (ImGui::SliderFloat("##slowrate", &settings.slowRate, 0.05f, 0.5f, "%.2fx")) {
                    playback.slow = true;
                    saveSettings(settings);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow-motion speed: 0.05 to 0.5 of normal. Ctrl+click to type one.");
            }
        }
        ImGui::End();

        if (showColorPreview) drawColorVisionPreview(&showColorPreview, settings.colorVision);

        // Pattern library dialogs.
        const SavePatternRequest saveRequest =
            drawSavePatternDialog(saveDialog, siteswapText, settingsForSaving(), myPatterns);
        if (saveRequest.save && (patternValid || sketching)) {
            storeMyPattern(saveRequest.name, saveRequest.replace);
            if (newAfterSave) newPatternDialog.openRequested = true;
            if (quitAfterSave) done = true;
            newAfterSave = quitAfterSave = false;
        } else if ((newAfterSave || quitAfterSave) && !saveDialog.openRequested &&
                   !ImGui::IsPopupOpen("Save to My Patterns")) {
            newAfterSave = quitAfterSave = false;  // the save was cancelled: so is the new pattern (or quitting)
        }
        const ManagePatternsRequest manageRequest =
            drawManagePatternsWindow(manageWindow, myPatterns, settings.colorVision);
        if (manageRequest.load >= 0) {
            const LibraryPattern chosen = myPatterns[static_cast<size_t>(manageRequest.load)];
            loadLibraryPattern(chosen, true);
        }
        if (manageRequest.rename >= 0) {
            LibraryPattern& renamed = myPatterns[static_cast<size_t>(manageRequest.rename)];
            const std::string oldName = renamed.displayName();
            const bool wasOpen = oldName == openPatternName;
            renamed.name = manageRequest.newName;
            if (wasOpen) openPatternName = renamed.displayName();
            saveMyPatterns();
            refreshRecentPattern(&recentPatterns, oldName, renamed);
            saveRecentPatterns(recentPatterns);
        }
        if (manageRequest.remove >= 0) {
            const std::string removedName = myPatterns[static_cast<size_t>(manageRequest.remove)].displayName();
            if (removedName == openPatternName) openPatternName.clear();
            myPatterns.erase(myPatterns.begin() + manageRequest.remove);
            saveMyPatterns();
            removeRecentPattern(&recentPatterns, removedName);
            saveRecentPatterns(recentPatterns);
        }
        // File > Save / Save As.
        if (saveRequested && (patternValid || sketching)) {
            if (openPatternIndex() >= 0) {
                storeMyPattern(openPatternName, true);
                siteswapBox.info = "Saved \"" + openPatternName + "\" to My Patterns.";
                siteswapBox.infoUntil = ImGui::GetTime() + 3.0;
            } else {
                saveAsRequested = true;
            }
        }
        if (saveAsRequested && (patternValid || sketching)) {
            std::snprintf(saveDialog.name, sizeof(saveDialog.name), "%s",
                          openPatternName.empty() ? siteswapText : openPatternName.c_str());
            saveDialog.openRequested = true;
        }
        saveRequested = saveAsRequested = false;
        if (!fileError.empty()) ImGui::OpenPopup("Couldn't save");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Couldn't save", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(fileError.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 6.0f, 0.0f))) {
                fileError.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (showImGuiDemo) ImGui::ShowDemoWindow(&showImGuiDemo);
        if (showShortcuts) drawShortcutsWindow(&showShortcuts);
        // Choosing among marks in the same place (see MarkChoice): say so, until done (Esc: keep
        // the one they're on).
        if (markChoice.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || !choreographyMode) {
                markChoice.active = false;
            } else {
                siteswapBox.info = "Spike marks " + [&]() {
                    std::string list;
                    for (size_t i = 0; i < markChoice.marks.size(); ++i)
                        list += (i == 0 ? "" : (i + 1 == markChoice.marks.size() ? " and " : ", ")) +
                                markNameOf(markChoice.marks[i]);
                    return list;
                }() + " are here: click the arrow (or name) of the one J" + std::to_string(markChoice.juggler + 1) +
                                   " stands on. (Esc: mark " + markNameOf(markChoice.pending.mark) + ".)";
                siteswapBox.infoUntil = ImGui::GetTime() + 0.1;
            }
        }
        // Selected spike marks: Esc clears them (as does leaving choreography mode); a change says
        // what to do with them, for a few seconds.
        selectedMarks.erase(std::remove_if(selectedMarks.begin(), selectedMarks.end(),
                                           [&](int m) { return m < 0 || m >= static_cast<int>(choreography.marks.size()); }),
                            selectedMarks.end());
        if (!choreographyMode || (!markChoice.active && !io.WantTextInput && markDrag < 0 &&
                                  !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            selectedMarks.clear();
        if (selectedGroup >= 0 && selectedMarks != choreography.group(selectedGroup)) {
            std::vector<int> sorted = selectedMarks;
            std::sort(sorted.begin(), sorted.end());
            if (sorted != choreography.group(selectedGroup)) selectedGroup = -1;
        }
        if (selectedMarks != selectedMarksSaid) {
            selectedMarksSaid = selectedMarks;
            if (!selectedMarks.empty()) {
                std::string list;
                for (size_t i = 0; i < selectedMarks.size(); ++i)
                    list += (i == 0 ? "" : (i + 1 == selectedMarks.size() ? " and " : ", ")) + markNameOf(selectedMarks[i]);
                siteswapBox.info = std::string(selectedMarks.size() == 1 ? "Spike mark " : "Spike marks ") + list +
                                   " selected: drag " + (selectedMarks.size() == 1 ? "it" : "one to move them all") +
                                   " (Shift: off the grid). Shift+click adds or removes a mark; Esc clears.";
                siteswapBox.infoUntil = ImGui::GetTime() + 5.0;
            }
        }
        // The Walk Like... window (see WalkLikeWindow).
        walkLike.previewing = false;
        if (walkLike.open) {
            const JugglingLoop loop = shownLoop();
            const int j = walkLike.juggler;
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, juggleParams);
            if (loop.empty() || j < 0 || j >= loop.jugglers || cycle <= 0) {
                walkLike.open = false;
            } else {
                const float em = ImGui::GetFontSize();
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                char title[48];
                std::snprintf(title, sizeof(title), "J%d Walks Like...###walk_like", j + 1);
                if (ImGui::Begin(title, &walkLike.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                    WalkLink link = choreography.link(j);
                    bool changed = false;
                    // Hovering a choice: show where it would put them.
                    auto previewIf = [&](const WalkLink& would) {
                        if (!ImGui::IsItemHovered() || would.leader < 0) return;
                        walkLike.previewing = true;
                        walkLike.preview = would;
                    };
                    // Leader: anyone whose walking doesn't already depend on this juggler.
                    ImGui::TextUnformatted("Leader:");
                    for (int k = 0; k < loop.jugglers; ++k) {
                        if (k == j) continue;
                        ImGui::SameLine();
                        const bool loops = choreography.dependsOn(k, j);
                        ImGui::BeginDisabled(loops);
                        char label[8];
                        std::snprintf(label, sizeof(label), "J%d", k + 1);
                        if (ImGui::RadioButton(label, link.leader == k) && link.leader != k) {
                            if (!link.active() && choreography.hasKeys(j)) {
                                walkLike.confirmLeader = k;  // ask first: their own keyframes go
                                walkLike.confirmRequested = true;
                            } else {
                                link.leader = k;
                                changed = true;
                            }
                        }
                        if (!loops) {
                            WalkLink would = link.active() ? link : WalkLink();
                            would.leader = k;
                            previewIf(would);
                        }
                        ImGui::EndDisabled();
                        if (loops && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("J%d already walks like J%d (perhaps through others).", k + 1, j + 1);
                    }
                    ImGui::BeginDisabled(!link.active());
                    // Time: J(j) on beat b does what the leader does on beat b + offset.
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Offset:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(em * 7.0f);
                    // (Offsets run over the whole choreography, rotated repeats and all.)
                    const int full = choreographyFullCycle(choreography, cycle);
                    int offset = link.offset;
                    if (ImGui::InputInt("##offset", &offset, 0, 0)) {
                        link.offset = ((offset % full) + full) % full;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        if (full != cycle)
                            ImGui::SetTooltip("J%d does on each beat what the leader does this many beats later\n"
                                              "(like a siteswap link's [k]). The choreography is %d beats written out,\n"
                                              "%d in all with its walking loop, so the offset can be up to %d.",
                                              j + 1, cycle, full, full - 1);
                        else
                            ImGui::SetTooltip("J%d does on each beat what the leader does this many beats later\n"
                                              "(like a siteswap link's [k]). The choreography is %d beats long.",
                                              j + 1, cycle);
                    }
                    // - and + (Ctrl: 4 at a time), each previewing where it would put them.
                    {
                        const float button = ImGui::GetFrameHeight();
                        const int step = ImGui::GetIO().KeyCtrl ? 4 : 1;
                        for (const int sign : {-1, 1}) {
                            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
                            const int to = ((link.offset + sign * step) % full + full) % full;
                            if (ImGui::Button(sign < 0 ? "-##offset" : "+##offset", ImVec2(button, button))) {
                                link.offset = to;
                                changed = true;
                            }
                            ImGui::PopItemFlag();
                            WalkLink would = link;
                            would.offset = to;
                            previewIf(would);
                        }
                        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                        ImGui::TextUnformatted("beats");
                    }
                    for (const int part : {2, 3, 4}) {
                        if (full % part != 0) continue;
                        ImGui::SameLine();
                        char label[16];
                        std::snprintf(label, sizeof(label), "1/%d (%d)", part, full / part);
                        if (ImGui::SmallButton(label)) {
                            link.offset = full / part;
                            changed = true;
                        }
                        WalkLink would = link;
                        would.offset = full / part;
                        previewIf(would);
                    }
                    // Past the beats written out, the leader is walking a part of the loop: say whose,
                    // and how far turned, as a check ("= J3's part 2 beats in, turned 1/4 CCW").
                    if (full != cycle && link.offset >= cycle && link.leader >= 0) {
                        const WalkLoop w = walkLoop(choreography, loop.jugglers);
                        const int times = link.offset / cycle, rest = link.offset % cycle;
                        if (w.valid && times < w.phases && link.leader < static_cast<int>(w.role[static_cast<size_t>(times)].size())) {
                            const int part = w.role[static_cast<size_t>(times)][static_cast<size_t>(link.leader)];
                            const Turn turned = w.turn[static_cast<size_t>(times)][static_cast<size_t>(link.leader)];
                            const std::string who =
                                part == link.leader ? std::string("the leader") : "J" + std::to_string(part + 1) + "'s part";
                            if (turned.none())
                                ImGui::TextDisabled("  = %s %d beats later", who.c_str(), rest);
                            else
                                ImGui::TextDisabled("  = %s %d beats later, turned %s", who.c_str(), rest, turnText(turned).c_str());
                        }
                    }
                    // Turn about the middle of the floor.
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Turned:");
                    ImGui::SameLine();
                    Turn hoveredTurn;
                    hoveredTurn.den = -1;  // (none hovered)
                    if (turnEditor("##turned", &link.turn, &hoveredTurn)) changed = true;
                    if (hoveredTurn.den > 0 && link.active()) {
                        walkLike.preview = link;
                        walkLike.preview.turn = hoveredTurn;
                        walkLike.previewing = true;
                    }
                    ImGui::TextDisabled("        about the middle of the floor, seen from above");
                    ImGui::EndDisabled();
                    if (changed && link.active()) setWalkLink(&choreography, j, link);
                    ImGui::Spacing();
                    ImGui::BeginDisabled(!link.active());
                    if (ImGui::Button("Unlink", ImVec2(em * 6.0f, 0.0f))) unlinkWalk(j);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Write J%d's walking out as their own keyframes (nothing moves).", j + 1);
                    ImGui::SameLine();
                    if (ImGui::Button("Close", ImVec2(em * 6.0f, 0.0f))) walkLike.open = false;

                    // Linking a juggler who has keyframes of their own: ask first.
                    if (walkLike.confirmRequested) {
                        ImGui::OpenPopup("Link this juggler?");
                        walkLike.confirmRequested = false;
                    }
                    if (ImGui::BeginPopupModal("Link this juggler?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        const size_t count = j < static_cast<int>(choreography.keys.size()) ? choreography.keys[static_cast<size_t>(j)].size() : 0;
                        ImGui::Text("J%d's %d keyframe%s will be deleted: J%d will walk like J%d instead.", j + 1,
                                    static_cast<int>(count), count == 1 ? "" : "s", j + 1, walkLike.confirmLeader + 1);
                        ImGui::Spacing();
                        if (ImGui::Button("Link", ImVec2(em * 6.0f, 0.0f))) {
                            WalkLink l;
                            l.leader = walkLike.confirmLeader;
                            setWalkLink(&choreography, j, l);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                            ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                }
                ImGui::End();
            }
        }
        // The loop column's builder (see LoopBuilder).
        if (loopBuilder.requested) {
            loopBuilder.requested = false;
            ImGui::OpenPopup("Walking Loop");
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        }
        if (ImGui::BeginPopupModal("Walking Loop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const JugglingLoop loop = shownLoop();
            const int j = loopBuilder.juggler;
            const int n = loop.empty() ? 0 : loop.jugglers;
            const int written = loop.empty() ? 0 : choreographyCycle(loop, juggleParams);
            if (j < 0 || j >= n) ImGui::CloseCurrentPopup();
            const float em = ImGui::GetFontSize();
            ImGui::Text("After beat %d, whoever's on C%d goes on to:", written, j + 1);
            for (int r = 0; r < n; ++r) {
                if (r > 0) ImGui::SameLine();
                char label[24];
                std::snprintf(label, sizeof(label), r == j ? "C%d (own)" : "C%d", r + 1);
                if (ImGui::RadioButton(label, loopBuilder.k.roleFor(j) == r)) loopBuilder.k.role = r == j ? -1 : r;
            }
            ImGui::TextUnformatted("...from its beat 1, turned:");
            turnEditor("##loopTurn", &loopBuilder.k.turn);
            ImGui::TextDisabled("about the middle of the floor, seen from above");
            const std::string text = continuationText(loopBuilder.k, j);
            ImGui::TextDisabled("In the cell: %s", text.empty() ? (std::to_string(j + 1) + " (its own path, unturned)").c_str()
                                                               : text.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Set", ImVec2(em * 6.0f, 0.0f))) {
                setContinuation(j, loopBuilder.k);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // Renaming a spike mark (see RenameMark).
        if (renameMark.requested) {
            renameMark.requested = false;
            if (renameMark.mark >= 0 && renameMark.mark < static_cast<int>(choreography.marks.size())) {
                std::snprintf(renameMark.text, sizeof(renameMark.text), "%s",
                              choreography.marks[static_cast<size_t>(renameMark.mark)].name.c_str());
                renameMark.error.clear();
                ImGui::OpenPopup("Rename Spike Mark");
                ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            }
        }
        if (ImGui::BeginPopupModal("Rename Spike Mark", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool exists = renameMark.mark >= 0 && renameMark.mark < static_cast<int>(choreography.marks.size());
            if (!exists) ImGui::CloseCurrentPopup();
            ImGui::TextUnformatted("Two characters, each 0-9 or a-z:");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0f);
            const bool entered = ImGui::InputText("##markName", renameMark.text, sizeof(renameMark.text),
                                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
            // Only 0-9 and a-z (capitals made small).
            {
                std::string kept;
                for (const char* ch = renameMark.text; *ch; ++ch) {
                    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(*ch)));
                    if ((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'z')) kept += lower;
                }
                std::snprintf(renameMark.text, sizeof(renameMark.text), "%s", kept.c_str());
            }
            if (!renameMark.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", renameMark.error.c_str());
            const float em = ImGui::GetFontSize();
            if ((ImGui::Button("Rename", ImVec2(em * 6.0f, 0.0f)) || entered) && exists) {
                const std::string name = renameMark.text;
                const int other = findMarkNamed(choreography, name);
                if (!validMarkName(name)) {
                    renameMark.error = "It needs two characters.";
                    plipWanted = true;
                } else if (other >= 0 && other != renameMark.mark) {
                    renameMark.error = "Another mark is called " + name + ".";
                    plipWanted = true;
                } else {
                    choreography.marks[static_cast<size_t>(renameMark.mark)].name = name;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // The Walk Like... window's preview: where the choice under the mouse would put them, now.
        WalkGhost walkGhost;
        if (walkLike.open && walkLike.previewing) {
            const JugglingLoop loop = shownLoop();
            const int j = walkLike.juggler;
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, juggleParams);
            if (cycle > 0 && j >= 0 && j < static_cast<int>(scene.jugglers.size()) && j < loop.jugglers) {
                Choreography trial = choreography;
                setWalkLink(&trial, j, walkLike.preview);
                JuggleParams params = juggleParams;
                params.choreography = &trial;
                Vec3 at;
                float yaw = 0.0f;
                jugglerPlacementAt(loop, params, j, playback.beat, &at, &yaw);
                walkGhost.show = true;
                walkGhost.pose = posedJuggler(scene.jugglers[static_cast<size_t>(j)], scene.prop);
                walkGhost.placement = Mat4::translation(at) * Mat4::rotationY(yaw);
                const ChoreographyPaths trialPaths = choreographyPaths(trial, loop.jugglers, cycle, choreographyScale(loop));
                if (j < static_cast<int>(trialPaths.lines.size())) walkGhost.path = trialPaths.lines[static_cast<size_t>(j)];
            }
        }
        // The loiter popup (see LoiterPopup).
        {
            const JugglingLoop loop = shownLoop();
            const int cycle = loop.empty() ? 0 : choreographyCycle(loop, juggleParams);
            if (loiter.openRequested) {
                loiter.openRequested = false;
                if (cycle > 0) ImGui::OpenPopup("##loiter");
            }
            ImGui::SetNextWindowPos(ImVec2(loiter.at.x + 16.0f, loiter.at.y + 16.0f), ImGuiCond_Appearing);
            const bool flashing = ImGui::GetTime() < loiter.errorUntil;
            const ImU32 errorColor = errorTextColor(settings.colorVision);
            if (flashing) {
                ImGui::PushStyleColor(ImGuiCol_Border, errorColor);
                ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 3.0f);
            }
            loiter.open = ImGui::BeginPopup("##loiter");
            if (flashing) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            if (loiter.open) {
                const int j = loiter.juggler;
                const int b = loiter.arrival.beat;
                // Whether a loiter of n beats fits: it mustn't reach (or pass) another of this
                // juggler's keyframes, and must be shorter than the choreography.
                auto fits = [&](int n, int* blockingBeat) {
                    if (n == 0) return true;
                    if (n >= cycle) {
                        if (blockingBeat) *blockingBeat = -1;
                        return false;
                    }
                    // With rotated repeats a loiter can't run on past the beats written out: the
                    // next beats are the first ones turned.
                    if (choreography.repeats() > 1 && b + n >= cycle) {
                        if (blockingBeat) *blockingBeat = -2;
                        return false;
                    }
                    if (j >= 0 && j < static_cast<int>(choreography.keys.size()))
                        for (const Keyframe& k : choreography.keys[static_cast<size_t>(j)]) {
                            const int d = ((k.beat - b) % cycle + cycle) % cycle;
                            if (k.beat != b && d >= 1 && d <= n) {
                                if (blockingBeat) *blockingBeat = d;
                                return false;
                            }
                        }
                    return true;
                };
                auto apply = [&](int n) {
                    int blocking = 0;
                    if (!fits(n, &blocking)) {
                        loiter.error = blocking == -2 ? "Too long: it would run past beat " + std::to_string(cycle) +
                                                            ", into the walking loop's copies."
                                       : blocking < 0 ? "Too long: the choreography is only " + std::to_string(cycle) + " beats."
                                                    : "No room: J" + std::to_string(j + 1) + " has a keyframe " +
                                                          std::to_string(blocking) + " beats later.";
                        loiter.errorUntil = ImGui::GetTime() + 1.5;
                        return;
                    }
                    if (n > 0) {
                        Keyframe stay = loiter.arrival;
                        stay.beat = (b + n) % cycle;
                        stay.longTurn = false;
                        setKeyframe(&choreography, j, stay);
                    }
                    ImGui::CloseCurrentPopup();
                };
                // Loiter lengths already in the choreography, nearest in time to this beat first
                // (beyond the 0-4 buttons); up to four, shortest first.
                std::vector<std::pair<int, int>> used;  // (distance in time, length)
                for (size_t jj = 0; jj < choreography.keys.size(); ++jj) {
                    const std::vector<Keyframe>& keys = choreography.keys[jj];
                    for (size_t i = 0; i < keys.size() && keys.size() > 1; ++i) {
                        const Keyframe& k = keys[i];
                        const Keyframe& next = keys[(i + 1) % keys.size()];
                        const int gap = ((next.beat - k.beat) % cycle + cycle) % cycle;
                        if (gap <= 4 || !sameKeyframeSpot(choreography, k, next)) continue;
                        if (static_cast<int>(jj) == j && k.beat == b) continue;  // (this one, if it's already a loiter)
                        const int d = ((k.beat - b) % cycle + cycle) % cycle;
                        used.emplace_back(std::min(d, cycle - d), gap);
                    }
                }
                std::sort(used.begin(), used.end());
                std::vector<int> recent;
                for (const std::pair<int, int>& u : used)
                    if (recent.size() < 4 && std::find(recent.begin(), recent.end(), u.second) == recent.end())
                        recent.push_back(u.second);
                std::sort(recent.begin(), recent.end());

                const std::string where = loiter.arrival.mark >= 0 ? "on mark " + markNameOf(loiter.arrival.mark) : "here";
                ImGui::Text("J%d arrives on beat %ld. Loiter %s for how many beats?", j + 1, loiter.onBeat + 1, where.c_str());
                const float em = ImGui::GetFontSize();
                auto choice = [&](int n) {
                    ImGui::PushID(n);
                    const bool ok = fits(n, nullptr);
                    ImGui::BeginDisabled(!ok);
                    if (ImGui::Button(std::to_string(n).c_str(), ImVec2(em * 2.2f, 0.0f))) apply(n);
                    ImGui::EndDisabled();
                    if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Doesn't fit: it would reach another of J%d's keyframes.", j + 1);
                    ImGui::PopID();
                    ImGui::SameLine();
                };
                for (int n = 0; n <= 4; ++n) choice(n);
                if (!recent.empty()) {
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                    for (const int n : recent) choice(n);
                }
                ImGui::NewLine();
                ImGui::SetNextItemWidth(em * 5.0f);
                const bool entered = ImGui::InputText("beats", loiter.text, sizeof(loiter.text),
                                                      ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
                const bool typing = ImGui::IsItemActive();
                ImGui::SameLine();
                if ((ImGui::Button("OK") || entered) && loiter.text[0] != '\0') apply(std::atoi(loiter.text));
                ImGui::TextDisabled("Or press a key: 0-9, a = 10, b = 11 ... (as in siteswap). Esc: just arrive.");
                if (ImGui::GetTime() < loiter.errorUntil)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorColor), "%s", loiter.error.c_str());
                if (!typing) {
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
                    for (const ImWchar c : io.InputQueueCharacters) {
                        int n = -1;
                        if (c >= '0' && c <= '9') n = c - '0';
                        else if (c >= 'a' && c <= 'z') n = 10 + (c - 'a');
                        else if (c >= 'A' && c <= 'Z') n = 10 + (c - 'A');
                        if (n >= 0) {
                            apply(n);
                            break;
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }
        // Help > 3D Mouse Diagnostics: what the cap is sending, live (-1..1 on each axis).
        if (showSpaceMouseDiagnostics) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("3D Mouse Diagnostics", &showSpaceMouseDiagnostics,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                if (!spaceMouse.present) {
                    ImGui::TextUnformatted("No 3D mouse found.");
                } else {
                    ImGui::TextUnformatted("Move the cap: what JuggleSim reads, -1 to 1 on each axis.");
                    ImGui::Text("Slide   right %+.2f   away %+.2f   up %+.2f", spaceMouse.move[0], spaceMouse.move[1],
                                spaceMouse.move[2]);
                    ImGui::Text("Turn    tilt %+.2f   roll %+.2f   spin %+.2f", spaceMouse.turn[0], spaceMouse.turn[1],
                                spaceMouse.turn[2]);
                    const bool idle = std::fabs(spaceMouse.turn[0]) < 0.06f && std::fabs(spaceMouse.move[1]) < 0.06f;
                    const bool tiltWins = std::fabs(spaceMouse.turn[0]) > 0.8f * std::fabs(spaceMouse.move[1]);
                    ImGui::Text("Tilt vs push/pull: %s", idle ? "-" : (tiltWins ? "tilt (turns the stage)" : "push/pull (moves)"));
                    ImGui::Text("Mouse-wheel events from the driver: %s",
                                ImGui::GetTime() - spaceMouseWheelBlocked < 1.0 ? "yes (ignored)" : "none seen");
                }
            }
            ImGui::End();
        }

        // File > New Pattern..., first offering to save the current pattern if it has changed.
        if (savePromptRequested) {
            ImGui::OpenPopup("Save changes?");
            savePromptRequested = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            ImGui::PushTextWrapPos(em * 34.0f);
            ImGui::Text("Save %s%s to My Patterns before %s?",
                        openPatternIndex() >= 0 ? ("\"" + openPatternName + "\"").c_str() : siteswapText,
                        savedChoreography != choreographyText() ? " (and its choreography)"
                        : savedThrowNext != throwNextText(throwNext) ? " (and its throwing loop)" : "",
                        promptForQuit ? "quitting" : "starting a new pattern");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            const bool open = openPatternIndex() >= 0;
            if (ImGui::Button(open ? "Save" : "Save...", ImVec2(em * 7.0f, 0.0f))) {
                if (open) {
                    // Over the open pattern, then on.
                    storeMyPattern(openPatternName, true);
                    if (promptForQuit)
                        done = true;
                    else
                        newPatternDialog.openRequested = true;
                } else {
                    std::snprintf(saveDialog.name, sizeof(saveDialog.name), "%s", siteswapText);
                    saveDialog.openRequested = true;
                    (promptForQuit ? quitAfterSave : newAfterSave) = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(em * 7.0f, 0.0f))) {
                if (promptForQuit)
                    done = true;
                else
                    newPatternDialog.openRequested = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 7.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (newPatternDialog.openRequested) {
            ImGui::OpenPopup("New Pattern");
            newPatternDialog.openRequested = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("New Pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            ImGui::PushTextWrapPos(em * 26.0f);
            ImGui::TextUnformatted("Starts a blank pattern: every throw is blank (not decided yet). Type throws "
                                   "into the grid, or on the ladder click a ? to draw a throw from it, then where "
                                   "it's caught, and so on.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Jugglers");
            ImGui::SameLine(em * 5.0f);
            for (int n = 1; n <= kMaxJugglers; ++n) {
                if (n > 1) ImGui::SameLine();
                char label[8];
                std::snprintf(label, sizeof(label), "%d", n);
                ImGui::RadioButton(label, &newPatternDialog.jugglers, n);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Period");
            ImGui::SameLine(em * 5.0f);
            ImGui::SetNextItemWidth(em * 8.0f);
            ImGui::InputInt("##period", &newPatternDialog.period);
            newPatternDialog.period = std::clamp(newPatternDialog.period, 1, kMaxLoopBeats);
            ImGui::SameLine();
            ImGui::TextDisabled("beats");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Count");
            ImGui::SameLine(em * 5.0f);
            ImGui::SetNextItemWidth(em * 8.0f);
            const std::string countName =
                newPatternDialog.count >= 2 ? std::to_string(newPatternDialog.count) + "-count" : std::string("None");
            if (ImGui::BeginCombo("##count", countName.c_str())) {
                if (ImGui::Selectable("None", newPatternDialog.count < 2)) newPatternDialog.count = 0;
                for (int n = 2; n <= 8; ++n)
                    if (ImGui::Selectable((std::to_string(n) + "-count").c_str(), newPatternDialog.count == n))
                        newPatternDialog.count = n;
                ImGui::EndCombo();
            }
            if (newPatternDialog.count >= 2) {
                ImGui::SameLine();
                ImGui::TextDisabled("(the period becomes a multiple of %d)", newPatternDialog.count);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Walking");
            ImGui::SameLine(em * 5.0f);
            ImGui::SetNextItemWidth(em * 8.0f);
            ImGui::InputInt("##walking", &newPatternDialog.walking);
            newPatternDialog.walking = std::clamp(newPatternDialog.walking, 0, kMaxChoreographyLength);
            ImGui::SameLine();
            ImGui::TextDisabled(newPatternDialog.walking == 0 ? "beats (0: no choreography yet)" : "beats");
            if (ImGui::IsItemHovered() || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("The choreography's own period: the beats of walking written out before its\n"
                                  "loop (a short passing pattern can have a long walk, and the other way round).");
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(em * 6.0f, 0.0f))) {
                JugglingLoop blank;
                const int n = newPatternDialog.count;
                blank.jugglers = newPatternDialog.jugglers;
                blank.period = n >= 2 ? (newPatternDialog.period + n - 1) / n * n : newPatternDialog.period;
                for (int j = 0; j < blank.jugglers; ++j)
                    for (int b = 0; b < blank.period; ++b)
                        blank.throws.push_back({n >= 2 && b % n != 0 ? 3 : kOpenThrow, j});
                std::string text;
                if (loopToText(blank, &text)) {
                    commitTyping();
                    playback.beat = 0.0;
                    recordSettingsChange();
                    const PatternSettings before = currentPatternSettings();
                    showPatternText(text);
                    choreography = Choreography();  // a new pattern starts in the default formation
                    if (newPatternDialog.walking > 0) {
                        // ...with spike marks under everyone, and the walking's own period.
                        // (A length of its own counts as a change: an untouched choreography follows the
                        // siteswap's period, which would put this one back to it.)
                        ensureSpikeMarks();
                        choreography.cycle = newPatternDialog.walking;
                        choreographyMode = true;
                    }
                    camera.free = false;
                    patternCount = n >= 2 ? n : 0;
                    throwNext.clear();
                    openPatternName.clear();
                    history.recordWithSettings(text, before, currentPatternSettings());
                    settingsRecorded();
                    markSaved();
                    selectedJuggler = -1;
                    frameSelected = false;
                    resetLadderView(ladderEdit);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(em * 6.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        pollSettingsChange();

        // The camera (after this frame's orbit and zoom), and the juggler numbers, drawn by ImGui
        // behind its windows but over the 3D view.
        const float jugglerAspect = viewSize.y > 0.0f ? viewSize.x / viewSize.y : 1.0f;
        // The top view frames the whole floor in use: the jugglers and the spike marks. (Not
        // while a mark is being dragged: the view would shift under the mouse.)
        // While a juggler or mark is being moved (or the loiter popup is up), it doesn't shrink,
        // and grows only slowly (pushing something against the edge widens the view gently), so
        // the floor doesn't shift under the mouse.
        {
            float half = 1.5f;
            for (const JugglerState& js : scene.jugglers) half = std::max(half, std::max(std::fabs(js.position.x), std::fabs(js.position.z)) + 0.8f);
            const float markScale = choreographyScale(loop);
            for (const SpikeMark& m : choreography.marks)
                half = std::max(half, std::max(std::fabs(m.x), std::fabs(m.z)) * markScale + 0.8f);
            const bool placing = jugglerDrag >= 0 || markDrag >= 0 || loiter.open;
            if (!placing)
                camera.floorHalf = half;
            else if (half > camera.floorHalf)
                camera.floorHalf += std::min(half - camera.floorHalf, 0.5f * io.DeltaTime);  // 0.5 m a second
        }
        // The 3D mouse, grabbing the stage: spin and tilt the cap to orbit; push it away to move
        // back (zoom out), pull it toward you to move in; slide it sideways or up and down and
        // the stage goes with it (which frees the camera). Each axis has a small dead zone and
        // rises gently, so small movements are fine.
        if (spaceMouse.present) {
            auto shaped = [](float v) {
                const float deadZone = 0.06f;
                const float a = std::fabs(v);
                if (a <= deadZone) return 0.0f;
                const float t = (a - deadZone) / (1.0f - deadZone);
                return std::copysign(t * t, v);
            };
            const float step = io.DeltaTime * settings.spaceMouseSpeed;
            float slideX = shaped(spaceMouse.move[0]), slideY = shaped(spaceMouse.move[1]), slideZ = shaped(spaceMouse.move[2]);
            const float spin = shaped(spaceMouse.turn[2]);
            float tilt = shaped(spaceMouse.turn[0]);
            // Tilting the cap toward or away from you pushes or pulls it too (often 60-100% as
            // far), and a push tilts it a little: the larger of the two (as read) is meant, and
            // the other is ignored.
            if (std::fabs(spaceMouse.turn[0]) > 0.8f * std::fabs(spaceMouse.move[1]))
                slideY = 0.0f;
            else
                tilt = 0.0f;
            // Turning the cap nudges it sideways too: while it's mostly being turned, its slides
            // are ignored (or a free camera, which moves faster the further away it is, can run
            // off).
            const float turnAmount = std::max(std::fabs(spin), std::fabs(tilt));
            const float slideAmount = std::max(std::fabs(slideX), std::max(std::fabs(slideY), std::fabs(slideZ)));
            if (turnAmount > 2.0f * slideAmount) slideX = slideY = slideZ = 0.0f;
            const bool turning = spin != 0.0f || tilt != 0.0f;
            const bool sliding = slideX != 0.0f || slideY != 0.0f || slideZ != 0.0f;
            // Anything done with the 3D mouse makes the camera free, from where it is.
            if (!camera.free && (turning || sliding)) {
                makeCameraFree(camera, lastCamera);
                spaceOrbitUntil = 0.0;
            }
            if (camera.free) {
                const double now = ImGui::GetTime();
                if (spin != 0.0f || tilt != 0.0f) {
                    // Spinning and tilting turn the stage like an object in your hand: the
                    // camera orbits round a center that stays put while the cap keeps turning
                    // (and a moment after), round the vertical for a spin and over the top for
                    // a tilt (never past straight down or up, so it never turns upside down).
                    if (now >= spaceOrbitUntil)
                        orbitCenter = freeOrbitCenter(lastCamera, scene, selectedJuggler, stageBox());
                    spaceOrbitUntil = now + 0.3;
                    orbitFreeCamera(camera, orbitCenter, -spin * 2.5f * step, tilt * 1.5f * step);
                }
                if (sliding) {
                    Vec3 forward, right, up;
                    freeCameraAxes(camera, &forward, &right, &up);
                    const float v = 1.5f * nearestToCamera(camera.freePos) * step;
                    camera.freePos = camera.freePos - right * (slideX * v) - up * (slideZ * v) - forward * (slideY * v);
                }
            }
        }
        const CameraView cameraView = updateCamera(camera, extents, jugglerAspect, io.DeltaTime);
        lastViewProj = cameraViewProj(cameraView, jugglerAspect);
        lastCamera = cameraView;
        lastAspect = jugglerAspect;
        // Floor overlays: their lines go in the 3D pass (overlayTriangles, drawn after the scene,
        // so bodies hide them); their text, here, where no body is in front of it.
        overlayTriangles.clear();
        {
            OverlayBuilder overlay(&overlayTriangles, cameraView, viewSize.y);
            const BodyOccluder bodies(scene, cameraView.position);
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            if (choreographyMode && choreography.active()) {
                const StageBox box = stageBox();
                buildFloorGrid(overlay, settings, box.min.x, box.min.z, box.max.x, box.max.z);
                std::vector<int> lit = markChoice.active ? markChoice.marks : std::vector<int>();
                lit.insert(lit.end(), selectedMarks.begin(), selectedMarks.end());
                buildSpikeMarks(overlay, choreography, choreographyScale(loop), markDrag, lit);
                drawSpikeMarkNumbers(bg, choreography, choreographyScale(loop), lastViewProj, viewMin, viewSize, bodies, lit,
                                     markGroupView());
            }
            if (showPaths) {
                const int pathSelected = loop.jugglers == 1 ? 0 : selectedJuggler;
                buildChoreographyPaths(overlay, paths, pathSelected, hoveredPathPoint);
                drawChoreographyPathLabels(bg, paths, pathCycle, lastViewProj, viewMin, viewSize, pathSelected,
                                           hoveredPathPoint, settings.pathBeatNumbers, bodies);
            }
            // The reticle: what the camera orbits about. Always shown (faintly) for a free
            // camera, brighter while orbiting; for the automatic framing, only while orbiting.
            const bool orbitingNow = cameraOrbiting || ImGui::GetTime() < spaceOrbitUntil;
            if (camera.free || orbitingNow) {
                const Vec3 center = !camera.free ? cameraView.target
                                                 : (orbitingNow ? orbitCenter
                                                                : freeOrbitCenter(cameraView, scene, selectedJuggler, stageBox()));
                buildOrbitReticle(overlay, center, orbitingNow);
            }
        }
        // The walking loop doesn't work: the stage is as it last was that did, tinted red round the edge.
        if (walkLoopBroken || throwLoopBroken) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            const ImVec2 b(viewMin.x + viewSize.x, viewMin.y + viewSize.y);
            bg->AddRectFilled(viewMin, b, IM_COL32(200, 40, 30, 22));
            for (int i = 0; i < 6; ++i) {
                const float inset = static_cast<float>(i) * 3.0f;
                bg->AddRect(ImVec2(viewMin.x + inset, viewMin.y + inset), ImVec2(b.x - inset, b.y - inset),
                            IM_COL32(220, 50, 40, 150 - i * 24), 0.0f, 0, 3.0f);
            }
            const char* says = throwLoopBroken ? (walkLoopBroken ? "Walking and throwing loops: see the grid"
                                                                 : "Throwing loop: see the grid")
                                               : "Walking loop: see the grid";
            bg->AddText(ImVec2(viewMin.x + 24.0f, b.y - ImGui::GetTextLineHeight() - 20.0f), IM_COL32(255, 140, 120, 255), says);
        }
        drawJugglerLabels(ImGui::GetBackgroundDrawList(), scene, lastViewProj, viewMin, viewSize, selectedJuggler,
                          &jugglerLabelCenters);
        if (settings.showThrowValues)
            drawPropValueLabels(ImGui::GetBackgroundDrawList(), scene, lastViewProj, viewMin, viewSize,
                                settings.colorVision, styleOfBall, parsed.form.relative);
        // The pattern's name, if it has one, in the top-left corner of the juggler pane.
        if (patternValid) {
            const std::map<std::string, std::string>::const_iterator named =
                patternNames.find(canonicalSiteswap(pattern));
            if (named != patternNames.end()) {
                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                const ImVec2 at(viewMin.x + panelMargin * 1.5f, viewMin.y + panelMargin);
                const char* name = named->second.c_str();
                bg->AddText(titleFont, kTitleFontSize, ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 160), name);
                bg->AddText(titleFont, kTitleFontSize, at, IM_COL32(232, 234, 240, 255), name);
            }
        }
        // A free camera stays where it's put; say so (and how to get the automatic framing back).
        if (camera.free) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            const char* note = "Free camera: Home re-frames";
            const ImVec2 size = ImGui::CalcTextSize(note);
            const ImVec2 at(viewMin.x + viewSize.x - size.x - panelMargin * 1.5f, viewMin.y + viewSize.y - size.y - panelMargin);
            bg->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 160), note);
            bg->AddText(at, IM_COL32(200, 204, 214, 220), note);
        }

        // The window's title: the open pattern (its name if it's one of mine, else its
        // siteswap), with a * while it has changes that aren't saved.
        {
            const bool unsaved = (patternValid || sketching) &&
                                 (savedPatternText != siteswapText || savedChoreography != choreographyText() || savedThrowNext != throwNextText(throwNext));
            const std::string shown = openPatternIndex() >= 0 ? openPatternName : std::string(siteswapText);
            const std::string title =
                shown.empty() ? std::string("JuggleSim") : shown + (unsaved ? "*" : "") + " \xe2\x80\x94 JuggleSim";
            if (title != windowTitle) {
                windowTitle = title;
                platformSetWindowTitle(title.c_str());
            }
        }

        // The plip: something the user just did was refused (a note under the siteswap grid, or
        // the loiter popup's) or broke the pattern. Once per thing, however it was refused.
        if (siteswapBox.noteUntil != lastNoteUntil) {
            if (siteswapBox.noteUntil > lastNoteUntil && !siteswapBox.note.empty()) plipWanted = true;
            lastNoteUntil = siteswapBox.noteUntil;
        }
        if (loiter.errorUntil != lastLoiterErrorUntil) {
            if (loiter.errorUntil > lastLoiterErrorUntil) plipWanted = true;
            lastLoiterErrorUntil = loiter.errorUntil;
        }
        // (One sound at a time: the plip wins over a tick.)
        if (plipWanted && settings.sounds) {
            const std::vector<unsigned char>& wav = plipWav();
            platformPlaySound(wav.data(), wav.size());
        } else if (tickWanted > 0) {
            const std::vector<unsigned char>& wav = tickWav(tickWanted == 2);
            platformPlaySound(wav.data(), wav.size());
        }
        plipWanted = false;
        tickWanted = 0;

        ImGui::Render();

        // --- Draw ---
        // OpenGL works in pixels and ImGui in points; on a high-DPI (Retina) display a point is
        // several pixels.
        int framebufferWidth = 0, framebufferHeight = 0;
        platformFramebufferSize(&framebufferWidth, &framebufferHeight);
        const float pixelsPerPointX = io.DisplaySize.x > 0.0f ? framebufferWidth / io.DisplaySize.x : 1.0f;
        const float pixelsPerPointY = io.DisplaySize.y > 0.0f ? framebufferHeight / io.DisplaySize.y : 1.0f;
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Right half, between the menu bar and the transport bar (above the siteswap entry).
        // GL's origin is bottom-left.
        const int viewX = static_cast<int>(std::lround(leftWidth * pixelsPerPointX));
        const int bottomPixels = static_cast<int>(std::lround((H - bodyBottom + transportHeight) * pixelsPerPointY));
        const int topPixels = static_cast<int>(std::lround(top * pixelsPerPointY));
        const GLRect jugglerRect{viewX, bottomPixels, framebufferWidth - viewX,
                                 framebufferHeight - topPixels - bottomPixels};
        renderJugglerView(renderer, prims, propMeshes, scene, cameraView, selectedJuggler, settings.colorVision, styleOfBall,
                          jugglerRect);
        if (jugglerRect.w > 0 && jugglerRect.h > 0) {
            glViewport(jugglerRect.x, jugglerRect.y, jugglerRect.w, jugglerRect.h);
            renderer.drawOverlay(cameraViewProj(cameraView, static_cast<float>(jugglerRect.w) / static_cast<float>(jugglerRect.h)),
                                 overlayTriangles);
            renderWalkGhost(renderer, prims, walkGhost, cameraView, jugglerRect, ImGui::GetTime());
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        platformSwapBuffers();
    }

    propMeshes.destroy();
    prims.destroy();
    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    return 0;
}
