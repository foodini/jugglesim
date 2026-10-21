// stage_overlays.h - what's drawn over and on the 3D view besides the jugglers: spike marks,
// choreography paths, the floor grid, the orbit reticle and labels, and picking things in the
// view with the mouse.
#pragma once

#include "camera.h"
#include "choreography.h"
#include "juggle_sim.h"
#include "juggler_figure.h"
#include "loop_ops.h"
#include "math3d.h"
#include "renderer.h"
#include "settings.h"
#include "imgui.h"

#include <string>
#include <vector>

constexpr float kMarkRadius = 0.22f;  // m: the mark's ring on the floor

// A spike mark's middle and arrow tip in the world.
void markGeometry(const SpikeMark& m, float scale, Vec3* middle, Vec3* tip);

// The mark under the mouse (and whether it's its arrow, for turning it), or -1. The arrow is
// anywhere along it outside the ring, or near its tip (a dozen pixels either side); otherwise the
// mark is anywhere inside its ring, on the floor (`floor`: the floor under the mouse, if
// `onFloor`), or within a few pixels of it. Arrows come first, then the mark nearest the mouse.
int pickSpikeMark(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize,
                  ImVec2 mouse, bool onFloor, Vec3 floor, bool* onArrow);

// What a walk link says, for the ladder and tooltips: "walks like J1 +24, 1/4 CCW".
std::string walkLinkLabel(const WalkLink& l);

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

// `written`: the beats written out; the paths run over the whole choreography (its walking loop
// and all).
ChoreographyPaths choreographyPaths(const Choreography& c, int jugglers, int written, float scale);

// The beats a path point is for, as shown: "5", or "5-8" for a stand-still.
std::string pathPointBeats(const PathPoint& p, int cycle);

Mat4 jugglerMatrix(const JugglerState& juggler);

JugglerPose posedJuggler(const JugglerState& juggler, PropType prop);

// The juggler under the mouse (the body parts as seen on screen, a little generously), or -1.
// The juggler under the mouse, or -1. `slack`: pixels added round each part (generous for
// picking a juggler; none for telling their body from a spike mark under their feet).
int pickJuggler(const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse,
                float slack = 6.0f);

// Juggler numbers ("J1", "J2", as on the ladder) over their heads (only drawn with two or more
// jugglers). The selected juggler's number is inverted (dark on light), so selection doesn't
// depend on color.
// Juggler number labels ("J1") over their heads. Labels that would overlap are pushed apart
// sideways, so two jugglers in the same place both show; a label pushed well away from its
// juggler gets a thin line down to them. *lastCenters (the labels' screen x last frame) keeps
// the labels in the same order while they're pushed apart, so they don't swap places as the
// jugglers pass each other.
void drawJugglerLabels(ImDrawList* dl, const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin,
                       ImVec2 rectSize, int selected, std::vector<float>* lastCenters);

struct Rgba {
    float r, g, b, a;
};

// The colour each group of spike marks is drawn in (with its dash pattern and number too, so it
// doesn't rest on colour alone).
Rgba markGroupColor(int g);

// A group's key: 1-9, and 0 for the tenth.
char markGroupKey(int g);

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

// The shortest distance between two line segments, p0-p1 and q0-q1.
float segmentDistance(Vec3 p0, Vec3 p1, Vec3 q0, Vec3 q1);

// Whether a juggler's body is between the eye and a point: the head, torso, arms and legs, as
// spheres and capsules a little fatter than drawn.
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

// Spike marks: a ring on the floor and an arrow the way it faces (lines), and its number (text).
// `hot` is drawn bolder.
void buildSpikeMarks(OverlayBuilder& overlay, const Choreography& c, float scale, int hot,
                     const std::vector<int>& highlighted);

// Where each spike mark's number bubble goes on screen: on the mark's middle (so it's never
// nearer another mark than its own); where bubbles would overlap (marks in the same place, or
// close), each moves out along its own arrow just far enough to clear the others (apart along
// the line between them, if two arrows point the same way). shown[i] false: off screen.
// (`extraH`: room under each bubble for its group badges, while they're shown.)
std::vector<ImVec2> markBubbleCenters(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin,
                                      ImVec2 rectSize, std::vector<bool>* shown, float extraH = 0.0f);

// The spike mark whose name bubble is under `mouse` (the one drawn last, if they overlap), or -1.
int markLabelUnder(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize,
                   ImVec2 mouse, float extraH = 0.0f);

// What's shown of the groups of spike marks: badges under every name (one column per group, up
// to the highest there is: "1 3 5"), and the groups whose marks are joined up by lines.
struct MarkGroupView {
    int slots = 0;                 // badge columns (0: no badges)
    std::vector<int> lines;        // the groups drawn
    float badgeH() const { return slots > 0 ? ImGui::GetTextLineHeight() + 2.0f : 0.0f; }
};

// The spike marks' numbers, in bubbles on their middles (see markBubbleCenters). Behind a body,
// they're dimmed to half, still readable (unless highlighted: then as usual). With `groups`: the
// groups' lines first (each group's marks joined, mark to nearest mark, between their names, so
// marks on one spot are told apart), and the badges under the names.
void drawSpikeMarkNumbers(ImDrawList* dl, const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin,
                          ImVec2 rectSize, const BodyOccluder& bodies, const std::vector<int>& highlighted,
                          const MarkGroupView& groups);

// The orbit reticle at `center`: a ring with a cross on the floor under it and, if the center
// is above the floor, a small three-axis cross there and a line down to the ring.
void buildOrbitReticle(OverlayBuilder& overlay, Vec3 center, bool active);

// The floor grid as a choreography keeps it (Choreography::grid): "<type>,<spacing>,<lines between
// mid-weight ones>,<spokes>,<spokes between mid-weight ones>,<half turn>" ("radial,0.5,4,8,0,1").
std::string floorGridText(const AppSettings& g);

// ...and back into the settings. False (nothing changed) if it can't be read.
bool readFloorGridText(const std::string& text, AppSettings* g);

// Snaps a floor point (world meters) to the nearest point of the grid: a crossing of the square
// grid's lines, a crossing of the radial grid's rings and spokes (or its middle), a corner of the
// triangle grid. With no grid, to the nearest 5 cm.
void snapToFloorGrid(const AppSettings& g, float* x, float* z);

// The radial grid's mid-weight spokes: every gridSpokeMajor-th spoke, if that divides the
// spokes evenly (else none: 0).
int effectiveSpokeMajor(const AppSettings& g);

// The grid's lines over the floor between (minX, minZ) and (maxX, maxZ), in three weights: fine
// lines, every gridMajorEvery-th line (or ring) mid-weight, and the lines through the middle of
// the floor heaviest, with a dot there.
void buildFloorGrid(OverlayBuilder& overlay, const AppSettings& g, float minX, float minZ, float maxX, float maxZ);

// The path point under the mouse (and not behind a body), or -1.
int pickPathPoint(const ChoreographyPaths& paths, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse,
                  const BodyOccluder& bodies);

// Choreography paths: everyone's dim blue-gray, the selected juggler's bright cyan (drawn last,
// on top), dashed. At each point a dot and an arrow the way the juggler faces.
void buildChoreographyPaths(OverlayBuilder& overlay, const ChoreographyPaths& paths, int selected, int hovered);

// ...and their text: each point's beats (showBeats, or the hovered point) and the juggler's
// number along each path.
void drawChoreographyPathLabels(ImDrawList* dl, const ChoreographyPaths& paths, int cycle, const Mat4& viewProj,
                                ImVec2 rectMin, ImVec2 rectSize, int selected, int hovered, bool showBeats,
                                const BodyOccluder& bodies);

// Throw values over the props in flight (and on held 2s), when the ladder's throw values are
// on: "3", "4p", as on the ladder, in a small box edged in the prop's color.
// The style (color, dash) a prop is drawn with: its own, or with "color by orbit" its orbit's
// (styleOfBall maps prop ids to style indexes; empty means each prop its own).
int propStyle(const std::vector<int>& styleOfBall, int ball);

void drawPropValueLabels(ImDrawList* dl, const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin,
                         ImVec2 rectSize, ColorVisionMode colorVision, const std::vector<int>& styleOfBall,
                         bool relativeTargets);
