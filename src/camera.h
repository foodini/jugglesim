// camera.h - the 3D view's camera: automatic framing of the pattern, the user's orbit and zoom on
// top of it (or a free camera), and projecting between the world and the screen.
#pragma once

#include "juggle_sim.h"
#include "math3d.h"
#include "imgui.h"

#include <algorithm>

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

// Pixel rectangle in GL convention (origin at the bottom-left of the window).
struct GLRect {
    int x, y, w, h;
};

constexpr float kCameraFovY = 35.0f * kPi / 180.0f;

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

struct CameraView {
    Vec3 position;
    Vec3 target;
    float farPlane;
};

// Back to the automatic framing: no orbit or zoom, not free.
void resetCamera(CameraControl& c);

// Makes the camera free, starting exactly where `view` (as last drawn) has it.
void makeCameraFree(CameraControl& c, const CameraView& view);

// Orbits the free camera about `center`: round the vertical through it by dyaw, and up or down
// by dpitch (positive: from higher up), turning the view with it, so the center stays where it
// is on screen.
void orbitFreeCamera(CameraControl& c, Vec3 center, float dyaw, float dpitch);

// The free camera's axes: forward (where it looks), right, and up.
void freeCameraAxes(const CameraControl& c, Vec3* forward, Vec3* right, Vec3* up);

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
Vec3 freeOrbitCenter(const CameraView& view, const JugglerScene& scene, int selected, const StageBox& stage);

// Advances the automatic framing toward `extents` and returns the camera to render with.
CameraView updateCamera(CameraControl& c, const SceneExtents& extents, float aspect, float dt);

Mat4 cameraViewProj(const CameraView& camera, float aspect);

// Where a world point lands in a screen rectangle (ImGui pixels, origin top-left), and how
// many pixels a meter covers there. False if the point is behind the camera.
bool projectToScreen(const Mat4& viewProj, Vec3 p, ImVec2 rectMin, ImVec2 rectSize, ImVec2* screen,
                     float* pixelsPerMeter = nullptr);

// The point on the floor (y = 0) under the mouse in a view of the scene (as rendered with
// cameraViewProj(view, aspect) into rect). False if the mouse is above the horizon.
bool floorUnderMouse(const CameraView& view, float aspect, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse, Vec3* out);
