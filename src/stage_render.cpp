// stage_render.cpp - see stage_render.h.

#include "stage_render.h"

#include "stage_overlays.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

namespace {

Vec3 colorToVec3(ImU32 c) {
    const ImVec4 f = ImGui::ColorConvertU32ToFloat4(c);
    return Vec3(f.x, f.y, f.z);
}

}  // namespace

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
namespace {

// `selected` is the selected juggler (-1 for none): drawn in gold (lighter than the others,
// so it shows without color vision too), on a lighter disc on the floor.
// Whether it's October (on this computer's clock).
bool isOctober() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    return local && local->tm_mon == 9;
}

// A torch's flame (see BallState::flame): the wick's recent places, each risen a little more
// the older it is and wavering, as a tapering glow in three layers (deep orange outside, a
// near-white core), so a moving torch leaves a trail of fire and a still one burns upward.
void appendFlame(std::vector<GlowVertex>& out, const BallState& b, double seconds, Vec3 cameraPos) {
    const size_t n = b.flame.size();
    if (n == 0) return;
    const float seed = static_cast<float>(b.ball) * 1.7f;
    const float t = static_cast<float>(seconds);
    // Flicker: the flame's height breathes, unevenly.
    const float tall = 0.85f + 0.12f * std::sin(t * 11.0f + seed) + 0.08f * std::sin(t * 23.0f + 2.1f * seed);
    const int kPoints = static_cast<int>(std::max<size_t>(n, 2));
    std::vector<Vec3> pts(static_cast<size_t>(kPoints));
    std::vector<float> frac(static_cast<size_t>(kPoints));
    for (int k = 0; k < kPoints; ++k) {
        const size_t i = std::min(static_cast<size_t>(k), n - 1);
        const float f = static_cast<float>(k) / static_cast<float>(kPoints - 1);  // 0 at the wick, 1 at the tip
        const float age = (static_cast<float>(k) + (n == 1 ? 1.0f : 0.0f)) * kFlameSampleSeconds * (n == 1 ? 6.0f : 1.0f);
        // Hot gas rises (and a bit faster as it goes), and wavers side to side.
        const float rise = tall * (1.1f * age + 2.5f * age * age);
        const Vec3 waver(std::sin(t * 13.0f + seed + k * 0.9f), 0.0f, std::cos(t * 11.0f + 1.3f * seed + k * 0.7f));
        pts[static_cast<size_t>(k)] = b.flame[i] + Vec3(0.0f, rise, 0.0f) + waver * (0.012f + 0.05f * f * f);
        frac[static_cast<size_t>(k)] = f;
    }
    // Each point's sideways direction (square to the flame and to the view), shared by the
    // pieces either side of it so the strip has no seams.
    std::vector<Vec3> across(static_cast<size_t>(kPoints));
    for (int k = 0; k < kPoints; ++k) {
        const Vec3 prev = pts[static_cast<size_t>(k > 0 ? k - 1 : k)], next = pts[static_cast<size_t>(k + 1 < kPoints ? k + 1 : k)];
        Vec3 along = next - prev;
        if (length(along) < 1e-5f) along = Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 c = cross(along, cameraPos - pts[static_cast<size_t>(k)]);
        across[static_cast<size_t>(k)] = length(c) > 1e-6f ? normalize(c) : Vec3(1.0f, 0.0f, 0.0f);
    }
    auto layer = [&](Vec3 color, float alpha, float width, float reach) {
        // A camera-facing strip, tapering to nothing at `reach` of the way along.
        auto halfWidth = [&](float f) { return 0.5f * width * std::pow(std::max(0.0f, 1.0f - f), 0.7f); };
        auto v = [&](Vec3 p, float a) { out.push_back({p.x, p.y, p.z, color.x, color.y, color.z, a}); };
        for (int k = 0; k + 1 < kPoints; ++k) {
            const float f0 = frac[static_cast<size_t>(k)] / reach, f1 = std::min(frac[static_cast<size_t>(k + 1)] / reach, 1.0f);
            if (f0 >= 1.0f) break;
            const Vec3 p0 = pts[static_cast<size_t>(k)], p1 = pts[static_cast<size_t>(k + 1)];
            const Vec3 s0 = across[static_cast<size_t>(k)] * halfWidth(f0), s1 = across[static_cast<size_t>(k + 1)] * halfWidth(f1);
            const float a0 = alpha * (1.0f - f0), a1 = alpha * (1.0f - f1);
            v(p0 - s0, a0); v(p0 + s0, a0); v(p1 + s1, a1);
            v(p0 - s0, a0); v(p1 + s1, a1); v(p1 - s1, a1);
        }
    };
    // The wick itself: a soft bright ball.
    {
        const Vec3 c = pts[0] + Vec3(0.0f, 0.015f, 0.0f);
        const Vec3 view = normalize(cameraPos - c);
        const Vec3 r = normalize(cross(view, Vec3(0.0f, 1.0f, 0.0f)));
        const Vec3 u = cross(r, view);
        const int kSegments = 12;
        const float radius = 0.055f * tall;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = static_cast<float>(i) / kSegments * 6.2831853f, a1 = static_cast<float>(i + 1) / kSegments * 6.2831853f;
            const Vec3 q0 = c + (r * std::cos(a0) + u * std::sin(a0)) * radius, q1 = c + (r * std::cos(a1) + u * std::sin(a1)) * radius;
            out.push_back({c.x, c.y, c.z, 1.0f, 0.75f, 0.35f, 0.75f});
            out.push_back({q0.x, q0.y, q0.z, 1.0f, 0.45f, 0.10f, 0.0f});
            out.push_back({q1.x, q1.y, q1.z, 1.0f, 0.45f, 0.10f, 0.0f});
        }
    }
    layer(Vec3(1.0f, 0.34f, 0.06f), 0.45f, 0.11f, 1.0f);
    layer(Vec3(1.0f, 0.62f, 0.18f), 0.55f, 0.065f, 0.85f);
    layer(Vec3(1.0f, 0.92f, 0.70f), 0.80f, 0.030f, 0.45f);
}

}  // namespace

void renderJugglerView(Renderer& renderer, const Primitives& prims, const PropMeshes& propMeshes,
                       const JugglerScene& scene, const CameraView& camera, int selected,
                       ColorVisionMode colorVision, const std::vector<int>& styleOfBall, const GLRect& rect,
                       bool firelight) {
    if (rect.w <= 0 || rect.h <= 0) return;

    glViewport(rect.x, rect.y, rect.w, rect.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.w, rect.h);
    if (firelight)
        glClearColor(0.025f, 0.027f, 0.04f, 1.0f);
    else
        glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 viewProj = cameraViewProj(camera, aspect);

    renderer.beginScene(viewProj, {-0.4f, -1.0f, -0.6f});
    std::vector<JugglerPose> poses;
    for (const JugglerState& juggler : scene.jugglers) poses.push_back(posedJuggler(juggler, scene.prop));
    // In October, by torchlight, the jugglers' heads are jack-o'-lanterns.
    const bool pumpkins = firelight && isOctober();
    // Torches, lit: a dark stage, each flame a flickering light (and each jack-o'-lantern a dim
    // one).
    if (firelight) {
        std::vector<Vec3> fires;
        std::vector<float> power;
        const float t = static_cast<float>(scene.seconds);
        for (size_t j = 0; pumpkins && j < poses.size() && static_cast<int>(fires.size()) < Renderer::kMaxFires; ++j) {
            const JugglerPose& pose = poses[j];
            fires.push_back(transformPoint(jugglerMatrix(scene.jugglers[j]), pose.head + normalize(pose.headForward) * 0.16f));
            power.push_back(0.10f * (0.9f + 0.1f * std::sin(t * 19.0f + static_cast<float>(j) * 2.9f)));
        }
        for (const BallState& b : scene.balls) {
            if (b.flame.empty() || static_cast<int>(fires.size()) >= Renderer::kMaxFires) continue;
            const float seed = static_cast<float>(b.ball) * 1.7f;
            fires.push_back(b.flame[0] + Vec3(0.0f, 0.08f, 0.0f));
            power.push_back(0.55f * (0.88f + 0.08f * std::sin(t * 17.0f + seed) + 0.05f * std::sin(t * 31.0f + 2.3f * seed)));
        }
        renderer.setFirelight(fires, power);
    }

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
        style.pumpkinHead = pumpkins;
        drawJuggler(renderer, prims, poses[j], jugglerMatrix(juggler), style);
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
    // Flames.
    for (const BallState& b : scene.balls)
        if (!b.flame.empty()) appendFlame(glow, b, scene.seconds, camera.position);
    renderer.drawGlow(viewProj, glow);
}
