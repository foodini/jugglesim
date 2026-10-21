// stage_overlays.cpp - see stage_overlays.h.

#include "stage_overlays.h"

#include "ladder_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr float kMarkArrow = 0.5f;    // m: from its middle to the tip of its facing arrow

}  // namespace

void markGeometry(const SpikeMark& m, float scale, Vec3* middle, Vec3* tip) {
    *middle = Vec3(m.x * scale, 0.0f, m.z * scale);
    *tip = *middle + Vec3(std::sin(m.yaw), 0.0f, std::cos(m.yaw)) * kMarkArrow;
}
namespace {

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b);  // (below)

}  // namespace

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

std::string walkLinkLabel(const WalkLink& l) {
    if (!l.active()) return std::string();
    std::string s = "walks like J" + std::to_string(l.leader + 1) + " " + (l.offset >= 0 ? "+" : "") + std::to_string(l.offset);
    if (!l.turn.none()) {
        const std::string turn = turnText(l.turn);  // ("1/4 CCW (90°)": without the degrees)
        s += ", " + turn.substr(0, turn.find(" ("));
    }
    return s;
}

namespace {

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

}  // namespace

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
    poseArmsForPalms(pose, juggler.palmRight, juggler.palmLeft, juggler.body.intensity, prop != PropType::Ball,
                     juggler.holdingRight ? &juggler.gripRight : nullptr, juggler.holdingLeft ? &juggler.gripLeft : nullptr);
    return pose;
}
namespace {

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float abx = b.x - a.x, aby = b.y - a.y;
    const float lengthSq = abx * abx + aby * aby;
    float t = lengthSq > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSq : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = a.x + abx * t - p.x, dy = a.y + aby * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

int pickJuggler(const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse,
                float slack) {
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

namespace {

Rgba rgb255(int r, int g, int b) { return Rgba{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}; }

}  // namespace

Rgba markGroupColor(int g) {
    static const Rgba kColors[Choreography::kMarkGroups] = {
        rgb255(86, 150, 230),  rgb255(242, 142, 43),  rgb255(225, 87, 89),   rgb255(118, 200, 190), rgb255(110, 190, 90),
        rgb255(200, 120, 220), rgb255(255, 157, 167), rgb255(190, 150, 110), rgb255(160, 200, 255), rgb255(210, 210, 210)};
    return kColors[((g % Choreography::kMarkGroups) + Choreography::kMarkGroups) % Choreography::kMarkGroups];
}
char markGroupKey(int g) { return g == 9 ? '0' : static_cast<char>('1' + g); }
namespace {

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

}  // namespace

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

namespace {

// Text with a dark edge, centered on (x) / beside a point, unless a body hides the point.
void overlayText(ImDrawList* dl, ImVec2 at, const char* text, Rgba color) {
    dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), toImU32(kOverlayEdge), text);
    dl->AddText(at, toImU32(color), text);
}

}  // namespace

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

std::vector<ImVec2> markBubbleCenters(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin,
                                      ImVec2 rectSize, std::vector<bool>* shown, float extraH) {
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
                    // Pointing nearly the same way: apart sideways, each to the side its arrow
                    // leans to (so the names come in the arrows' order); along the line between
                    // them if the arrows are the same.
                    const float sx = along[k].x - along[i].x, sy = along[k].y - along[i].y;
                    const float slen = std::hypot(sx, sy);
                    const float len = std::hypot(dx, dy);
                    float ux = 0.0f, uy = 0.0f;
                    if (slen > 0.02f) {
                        ux = sx / slen;
                        uy = sy / slen;
                    } else if (len > 0.5f) {
                        ux = dx / len;
                        uy = dy / len;
                    } else {
                        ux = -along[i].y;
                        uy = along[i].x;
                    }
                    const float need = 0.5f * (std::fabs(ux) * w + std::fabs(uy) * h) - 0.5f * (dx * ux + dy * uy) + 1.0f;
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

int markLabelUnder(const Choreography& c, float scale, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize,
                   ImVec2 mouse, float extraH) {
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

namespace {

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

}  // namespace

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
    for (int pass = 0; pass < 2; ++pass)
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
        if (pass == 0) {
            // A name moved off its mark's middle (marks sharing a spot) gets a thin line to its own
            // arrow, so which name is which arrow's is plain.
            {
                ImVec2 mid, onArrow;
                if (projectToScreen(viewProj, middle, rectMin, rectSize, &mid) &&
                    projectToScreen(viewProj, middle + (tip - middle) * 0.7f, rectMin, rectSize, &onArrow) &&
                    std::hypot(at[i].x - mid.x, at[i].y - mid.y) > em * 0.8f) {
                    dl->AddLine(at[i], onArrow, faded(IM_COL32(10, 10, 14, 200)), 3.5f);
                    dl->AddLine(at[i], onArrow, faded(lit ? IM_COL32(255, 255, 255, 230) : toImU32(kMarkColor)), 1.5f);
                    dl->AddCircleFilled(onArrow, 2.5f, faded(lit ? IM_COL32(255, 255, 255, 230) : toImU32(kMarkColor)));
                }
            }
            continue;
        }
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
namespace {

// ---- The floor grid (choreography) ----

const Rgba kGridColor{0.62f, 0.64f, 0.70f, 0.38f};        // faint neutral gray, under everything else
const Rgba kGridMidColor{0.70f, 0.72f, 0.78f, 0.62f};     // every few lines
const Rgba kGridOriginColor{0.82f, 0.84f, 0.90f, 0.90f};  // through the middle of the floor

}  // namespace

std::string floorGridText(const AppSettings& g) {
    static const char* const kTypes[] = {"none", "square", "radial", "triangles"};
    const int type = std::clamp(static_cast<int>(g.floorGrid), 0, 3);
    char text[96];
    std::snprintf(text, sizeof(text), "%s,%g,%d,%d,%d,%d", kTypes[type], static_cast<double>(g.gridSpacing),
                  g.gridMajorEvery, g.gridSpokes, g.gridSpokeMajor, g.gridHalfTurn ? 1 : 0);
    return text;
}
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

int effectiveSpokeMajor(const AppSettings& g) {
    const int spokes = std::clamp(g.gridSpokes, 3, 32);
    const int n = g.gridSpokeMajor;
    return n >= 2 && n < spokes && spokes % n == 0 ? n : 0;
}

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

int propStyle(const std::vector<int>& styleOfBall, int ball) {
    return ball >= 0 && ball < static_cast<int>(styleOfBall.size()) ? styleOfBall[static_cast<size_t>(ball)] : ball;
}

void drawPropValueLabels(ImDrawList* dl, const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin,
                         ImVec2 rectSize, ColorVisionMode colorVision, const std::vector<int>& styleOfBall,
                         bool relativeTargets) {
    dl->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);
    const float em = ImGui::GetFontSize();
    const int jugglers = static_cast<int>(scene.jugglers.size());
    // Above the prop: clear of a ball, or of a club, ring or hoop, whichever way it's turned.
    const float above = scene.prop == PropType::Ball  ? kBallRadius + 0.05f
                        : scene.prop == PropType::Hoop ? kHoopRadius + kHoopTubeRadius + 0.05f
                                                       : 0.34f;
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
