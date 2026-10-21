// camera.cpp - see camera.h.

#include "camera.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kJugglerTopY = 1.80f;  // a little above the top of the juggler's head
// A ring of 3+ jugglers is seen from a little above, so the near ones don't hide the far ones.
constexpr float kRingPitch = 0.50f;   // radians (about 29 degrees)
constexpr float kRingMargin = 1.15f;  // extra distance, so the nearest jugglers fit

}  // namespace

namespace {

// The direction from what the camera looks at back to the camera, for a yaw and pitch.
Vec3 cameraBackDir(float yaw, float pitch) {
    return Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

}  // namespace

void resetCamera(CameraControl& c) {
    c.yaw = 0.0f;
    c.pitch = 0.0f;
    c.zoom = 1.0f;
    c.free = false;
}

void makeCameraFree(CameraControl& c, const CameraView& view) {
    const Vec3 back = normalize(view.position - view.target);
    c.freePos = view.position;
    c.freePitch = std::asin(std::clamp(back.y, -1.0f, 1.0f));
    c.freeYaw = std::atan2(back.x, back.z);
    c.free = true;
    c.topView = false;
    c.topBlend = 0.0f;
}

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

void freeCameraAxes(const CameraControl& c, Vec3* forward, Vec3* right, Vec3* up) {
    *forward = -cameraBackDir(c.freeYaw, c.freePitch);
    *right = normalize(cross(*forward, Vec3(0.0f, 1.0f, 0.0f)));
    *up = cross(*right, *forward);
}

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

bool projectToScreen(const Mat4& viewProj, Vec3 p, ImVec2 rectMin, ImVec2 rectSize, ImVec2* screen,
                     float* pixelsPerMeter) {
    const float x = viewProj.at(0, 0) * p.x + viewProj.at(0, 1) * p.y + viewProj.at(0, 2) * p.z + viewProj.at(0, 3);
    const float y = viewProj.at(1, 0) * p.x + viewProj.at(1, 1) * p.y + viewProj.at(1, 2) * p.z + viewProj.at(1, 3);
    const float w = viewProj.at(3, 0) * p.x + viewProj.at(3, 1) * p.y + viewProj.at(3, 2) * p.z + viewProj.at(3, 3);
    if (w < 0.05f) return false;
    screen->x = rectMin.x + (x / w * 0.5f + 0.5f) * rectSize.x;
    screen->y = rectMin.y + (0.5f - y / w * 0.5f) * rectSize.y;
    if (pixelsPerMeter) *pixelsPerMeter = 0.5f * rectSize.y / (std::tan(kCameraFovY * 0.5f) * w);
    return true;
}

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
