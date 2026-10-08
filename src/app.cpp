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
#include "siteswap.h"
#include "siteswap_edit.h"

#include "imgui.h"
#include "imgui_internal.h"  // to open a slider's text field on double-click (fineSlider)
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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
};

struct CameraView {
    Vec3 position;
    Vec3 target;
    float farPlane;
};

void resetCamera(CameraControl& c) {
    c.yaw = 0.0f;
    c.pitch = 0.0f;
    c.zoom = 1.0f;
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
    if (!c.framed) {
        c.centerX = extents.centerX;
        c.centerY = wantCenterY;
        c.centerZ = extents.centerZ;
        c.bottomY = bottom;
        c.distance = wantDistance;
        c.basePitch = wantPitch;
        c.framed = true;
    } else {
        const float ease = 1.0f - std::exp(-dt * 5.0f);
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
    const float pitch = std::clamp(c.pitch + c.basePitch, -1.45f, 1.45f);
    const Vec3 dir(std::sin(c.yaw) * std::cos(pitch), std::sin(pitch), std::cos(c.yaw) * std::cos(pitch));
    v.position = v.target + dir * d;
    v.farPlane = d * 3.0f + 50.0f;
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
int pickJuggler(const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectSize, ImVec2 mouse) {
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
            const float reach = std::max(part.radius * ppm, 0.0f) + 6.0f;  // a few pixels of slack
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
void drawJugglerLabels(ImDrawList* dl, const JugglerScene& scene, const Mat4& viewProj, ImVec2 rectMin,
                       ImVec2 rectSize, int selected) {
    if (scene.jugglers.size() < 2) return;
    dl->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);
    const float em = ImGui::GetFontSize();
    for (size_t j = 0; j < scene.jugglers.size(); ++j) {
        const JugglerPose pose = posedJuggler(scene.jugglers[j], scene.prop);
        const Vec3 above = transformPoint(jugglerMatrix(scene.jugglers[j]), pose.head) +
                           Vec3(0.0f, pose.headRadius + 0.08f, 0.0f);
        ImVec2 anchor;
        if (!projectToScreen(viewProj, above, rectMin, rectSize, &anchor)) continue;
        char text[16];
        std::snprintf(text, sizeof(text), "J%d", static_cast<int>(j) + 1);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float padX = em * 0.45f, padY = em * 0.15f;
        const float boxWidth = std::max(textSize.x + 2.0f * padX, textSize.y + 2.0f * padY);
        const ImVec2 boxMin(anchor.x - boxWidth * 0.5f, anchor.y - textSize.y - 2.0f * padY);
        const ImVec2 boxMax(anchor.x + boxWidth * 0.5f, anchor.y);
        const bool isSelected = static_cast<int>(j) == selected;
        const float rounding = em * 0.3f;
        dl->AddRectFilled(boxMin, boxMax, isSelected ? IM_COL32(236, 238, 244, 255) : IM_COL32(28, 30, 36, 210),
                          rounding);
        dl->AddRect(boxMin, boxMax, IM_COL32(210, 214, 224, 230), rounding, 0, isSelected ? 2.0f : 1.0f);
        dl->AddText(ImVec2(anchor.x - textSize.x * 0.5f, boxMin.y + padY),
                    isSelected ? IM_COL32(16, 16, 20, 255) : IM_COL32(232, 234, 240, 255), text);
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
                         TweakablesPanelState& state) {
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
            if (ImGui::SmallButton("Reset")) resetTweakables(params);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("150 BPM, dwell 1.40 beats, automatic distance");

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
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Pick the distance from the pattern's highest throw.\n"
                                      "Moving the slider turns this off.");
            }
        }
    }
    ImGui::End();
    if (toggled) collapsed = !collapsed;
    return toggled;
}

Vec3 colorToVec3(ImU32 c) {
    const ImVec4 f = ImGui::ColorConvertU32ToFloat4(c);
    return Vec3(f.x, f.y, f.z);
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
enum class TransportIcon { StepBack, Play, Pause, StepForward };

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
};
constexpr double kStepBeats = 1.0 / 12.0;  // one arrow-key press; Shift steps a whole beat

// ---- The siteswap text box's helpers (the text work itself is in siteswap_edit.h)

// What the text box's callback works with, kept between frames.
struct SiteswapBox {
    std::string before;          // the text before this frame's edit (for autofill)
    bool swapRequested = false;  // Ctrl+T (Cmd+T on a Mac) this frame
    int rotateRequested = 0;     // Ctrl+R (+1, right) or Ctrl+L (-1, left) this frame
    std::string note;            // why the last swap didn't happen
    double noteUntil = 0.0;      // (shown until then)
    bool wasActive = false;      // the box had the keyboard last frame
    SiteswapAutofillMemory memory;  // a column deletion the next keystroke may take back
};

// The text box's callback: autofill after an edit, Up/Down to change the throw at the cursor,
// and the swap.
int siteswapBoxCallback(ImGuiInputTextCallbackData* data) {
    SiteswapBox* box = static_cast<SiteswapBox*>(data->UserData);
    const std::string text(data->Buf, static_cast<size_t>(data->BufTextLen));
    SiteswapEdit edit;
    bool apply = false;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
        apply = autofillSiteswap(box->before, text, data->CursorPos, &edit, &box->memory);
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        box->memory.forget();
        const int delta = data->EventKey == ImGuiKey_UpArrow ? 1 : -1;
        if (ImGui::GetIO().KeyShift) {
            // Shift+Up/Down: where the throw goes (self, then a pass to each juggler in turn).
            std::string why;
            apply = stepPassTarget(text, data->CursorPos, delta, &edit, &why);
            if (!apply && !why.empty()) {
                box->note = why;
                box->noteUntil = ImGui::GetTime() + 4.0;
            }
        } else {
            apply = bumpThrowAtCursor(text, data->CursorPos, delta, &edit);
        }
    }
    // Moving the cursor away means the deletion wasn't the start of a replacement.
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && box->memory.armed &&
        (data->CursorPos != box->memory.cursor || data->SelectionStart != data->SelectionEnd))
        box->memory.forget();
    if (!apply && box->swapRequested) {
        box->memory.forget();
        std::string why;
        apply = swapThrows(text, data->SelectionStart, data->SelectionEnd, data->CursorPos, &edit, &why);
        if (!apply) {
            box->note = why;
            box->noteUntil = ImGui::GetTime() + 4.0;
        }
    }
    box->swapRequested = false;
    if (!apply && box->rotateRequested != 0) {
        box->memory.forget();
        std::string why;
        apply = rotateThrows(text, box->rotateRequested, data->CursorPos, &edit, &why);
        if (!apply) {
            box->note = why;
            box->noteUntil = ImGui::GetTime() + 4.0;
        }
    }
    box->rotateRequested = 0;
    if (apply && static_cast<int>(edit.text.size()) < data->BufSize) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, edit.text.c_str());
        data->CursorPos = std::min(edit.cursor, data->BufTextLen);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        box->before = edit.text;
    } else {
        box->before = text;
    }
    return 0;
}

// A throw described for the text box's tooltip: "J1, beat 3, right hand: 4p, a pass to J2's
// left hand, caught on beat 7." Beats count from 1, as on the ladder; each juggler's first
// throw is from the right hand.
std::string describeThrow(const JugglingLoop& loop, int juggler, int beat) {
    const LoopThrow& t = loop.at(juggler, beat);
    auto handName = [](int b) { return (b % 2 == 0) ? "right" : "left"; };
    char text[160];
    int n = 0;
    if (loop.jugglers > 1) n = std::snprintf(text, sizeof(text), "J%d, ", juggler + 1);
    n += std::snprintf(text + n, sizeof(text) - static_cast<size_t>(n), "beat %d, %s hand: ", beat + 1, handName(beat));
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
                          handName(caught), caught + 1);
        else if (caught % 2 == beat % 2)
            std::snprintf(rest, room, "%c, back to the same hand on beat %d", digit, caught + 1);
        else
            std::snprintf(rest, room, "%c, across to the %s hand on beat %d", digit, handName(caught), caught + 1);
    }
    return text;
}

// The x position of character `index` of the text box's text (as drawn, scrolled or not).
float siteswapCharacterX(const char* text, int index, float textLeft) {
    return textLeft + ImGui::CalcTextSize(text, text + index).x;
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
    char siteswapText[256] = "531";
    SiteswapBox siteswapBox;
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
    // The pattern as last loaded, saved or started: File > New Pattern... offers to save first
    // if what's being juggled has changed since (tempo, dwell and distance don't count).
    std::string savedPatternText = siteswapText;
    bool savePromptRequested = false;
    bool newAfterSave = false;  // Save... was chosen in that prompt: start the new pattern once saved
    LadderEditState ladderEdit;  // drag-editing and pan/zoom state for the ladder
    PatternHistory history;
    history.current = siteswapText;

    JuggleParams juggleParams;
    juggleParams.prop = settings.prop;

    // The settings a library pattern can carry, as they are now (all of them).
    auto currentPatternSettings = [&]() -> PatternSettings {
        PatternSettings current;
        current.hasProp = current.hasTempo = current.hasDwell = current.hasDistance = true;
        current.prop = juggleParams.prop;
        current.tempo = juggleParams.bpm;
        current.dwell = juggleParams.dwellBeats;
        current.distance = juggleParams.distance;
        return current;
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
        if (ImGui::IsAnyItemActive()) settingsChangedAt = ImGui::GetTime();
        if (now != recordedSettings && ImGui::GetTime() - settingsChangedAt >= kSettingsSettleSeconds)
            recordSettingsChange();
    };
    // After undo/redo or a library load: what's showing is what's recorded.
    auto settingsRecorded = [&]() { recordedSettings = lastSeenSettings = currentPatternSettings(); };
    auto recordPattern = [&](const std::string& text) {
        recordSettingsChange();
        history.record(text);
    };
    // Typing in the siteswap box becomes an undo step when the box loses focus, but a change
    // made some other way (on the ladder, the period buttons, the library) can come first. Each
    // of those calls this, so what was typed is a step of its own and undoing the change goes
    // back to it rather than to whatever was there before the typing.
    auto commitTyping = [&]() {
        if ((parsed.valid || parsed.sketch) && history.current != siteswapText) recordPattern(siteswapText);
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
    auto applyLoop = [&](const JugglingLoop& changed) {
        commitTyping();
        JugglingLoop l = changed;
        if (openThrowCount(l) == 0) l = loopWithPeriod(l, loopShortestPeriod(l));
        std::string text;
        // Written the way the pattern's passes were (relative targets if it used them).
        if (!loopToText(l, &text, &parsed.passStyle)) return;
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
    auto undo = [&]() {
        if (ladderEdit.chain.active) {  // an open chain is just cancelled, like Esc
            cancelLadderEdit(ladderEdit);
            return;
        }
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
    PatternMenuState juggleSimMenuState, myMenuState;

    // Loads a pattern from the library: its siteswap, and whatever settings it carries. One
    // undo step, which also puts the settings back. It goes to the top of Recent.
    auto loadLibraryPattern = [&](const LibraryPattern& chosen, bool mine) {
        commitTyping();
        recordSettingsChange();
        const PatternSettings before = currentPatternSettings();
        showPatternText(chosen.siteswap);
        applyPatternSettings(chosen.settings);
        history.recordWithSettings(siteswapText, before, currentPatternSettings());
        settingsRecorded();
        savedPatternText = siteswapText;
        addRecentPattern(&recentPatterns, chosen, mine);
        saveRecentPatterns(recentPatterns);  // not worth bothering the user if this fails
    };

    SavePatternDialog saveDialog;
    // File > New Pattern...: a blank sketch (every throw "?") for some jugglers and period.
    struct NewPatternDialog {
        bool openRequested = false;
        int jugglers = 2;
        int period = 6;
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

    TweakablesPanelState tweakablesPanel;
    bool showImGuiDemo = false;
    bool showColorPreview = false;
    Playback playback;
    CameraControl camera;
    // Selection in the 3D view (passing patterns): the selected juggler, or -1. With
    // frameSelected the camera frames just that juggler.
    int selectedJuggler = -1;
    bool frameSelected = false;
    Mat4 lastViewProj = Mat4::identity();  // the 3D view's camera as last drawn (for picking)
    // Pattern extents depend only on the loop, timing and framing, so they're cached.
    JugglingLoop extentsLoop;
    JuggleParams extentsParams{-1.0, -1.0};
    int extentsJuggler = -2;
    SceneExtents extents;

    auto stepPlayback = [&](double beats) {
        playback.playing = false;
        playback.beat += beats;
    };

    bool done = false;
    while (!done) {
        if (!platformPollEvents()) break;
        if (platformIsMinimized()) {
            platformSleepMilliseconds(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        platformNewFrame();
        ImGui::NewFrame();

        // --- Playback clock ---
        if (playback.playing) playback.beat += static_cast<double>(io.DeltaTime) * juggleParams.bpm / 60.0;

        // --- Keyboard shortcuts (not while typing: the text box has its own undo, etc.) ---
        if (!io.WantTextInput && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift) undo();
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift)) redo();
        }
        if (!io.WantTextInput && !io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) playback.playing = !playback.playing;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameSelected = selectedJuggler >= 0;
            if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                settings.colorByOrbit = !settings.colorByOrbit;
                saveSettings(settings);
            }
            // C: collapse all the jugglers' ladder strips (or expand them, if all are collapsed).
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                const int jugglers = sketching ? sketchLoop.jugglers : (patternValid ? pattern.jugglers : 0);
                if (jugglers > 1) toggleCollapseAll(ladderEdit, jugglers);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                settings.showThrowValues = !settings.showThrowValues;
                saveSettings(settings);
            }
        }

        // --- Main menu bar ---
        float menuHeight = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Pattern...")) {
                    if ((patternValid || sketching) && savedPatternText != siteswapText)
                        savePromptRequested = true;
                    else
                        newPatternDialog.openRequested = true;
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
                        const LibraryPattern chosen = *choice.pattern;
                        loadLibraryPattern(chosen, choice.mine);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save to My Patterns...", nullptr, false, patternValid || sketching)) {
                    std::snprintf(saveDialog.name, sizeof(saveDialog.name), "%s", siteswapText);
                    saveDialog.openRequested = true;
                }
                ImGui::MenuItem("Manage My Patterns...", nullptr, &manageWindow.open);
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) done = true;
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
                if (ImGui::MenuItem("Frame All", nullptr, false, frameSelected)) frameSelected = false;
                if (ImGui::MenuItem("Frame Selected", "F", false, selectedJuggler >= 0))
                    frameSelected = true;
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
                ImGui::Separator();
                ImGui::MenuItem("ImGui demo window", nullptr, &showImGuiDemo);
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
        const float entryHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeight() +
                                  style.WindowPadding.y * 2.0f;
        const ImGuiWindowFlags paneFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;

        // Ladder diagram (top of left half).
        ImGui::SetNextWindowPos(ImVec2(0.0f, top));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, H - top - entryHeight));
        // The ladder handles the mouse wheel itself (pan/zoom), so the pane mustn't scroll.
        ladderPreview = false;
        if (ImGui::Begin("Ladder", nullptr, paneFlags | ImGuiWindowFlags_NoScrollWithMouse)) {
            // What the ladder shows and edits: the sketch, or the pattern.
            const JugglingLoop ladderLoop = sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop());
            const LadderToolbarRequest request =
                drawLadderToolbar(ladderLoop, ladderEdit, settings.showThrowValues, settings.colorByOrbit);
            if (request.toggleOrbits) {
                settings.colorByOrbit = !settings.colorByOrbit;
                saveSettings(settings);
            }
            if (request.toggleCollapseAll) toggleCollapseAll(ladderEdit, ladderLoop.jugglers);
            if (request.resetView) resetLadderView(ladderEdit);
            if (request.toggleValues) {
                settings.showThrowValues = !settings.showThrowValues;
                saveSettings(settings);
            }
            if (request.newPeriodBeats > 0) {
                cancelLadderEdit(ladderEdit);
                commitTyping();
                const JugglingLoop longer = loopWithPeriod(ladderLoop, request.newPeriodBeats);
                // (Written as it is: a period chosen on purpose isn't shortened again.)
                std::string text;
                if (loopToText(longer, &text, &parsed.passStyle)) {
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
            ladderOptions.relativeTargets = parsed.passStyle.relative;
            const LadderEditResult edited =
                drawLadderDiagram(sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop()),
                                  settings.colorVision, ladderEdit, playback.beat, ladderOptions);
            // Picking up a throw pauses the jugglers (don't confuse them mid-edit); drawing in a
            // sketch doesn't, so you can watch props pop in as you go.
            if (edited.startedChain) playback.playing = false;
            ladderPreview = edited.hasPreview;
            if (ladderPreview) ladderPreviewLoop = edited.preview;
            if (edited.selectJuggler != kNoSelectionChange) {
                selectedJuggler = edited.selectJuggler;
                if (selectedJuggler < 0) frameSelected = false;
            }
            if (edited.committed) applyLoop(edited.loop);
        }
        ImGui::End();

        // Siteswap entry (bottom of left half).
        ImGui::SetNextWindowPos(ImVec2(0.0f, H - entryHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, entryHeight));
        if (ImGui::Begin("Siteswap entry", nullptr, paneFlags)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Siteswap");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            // Autofill as you type, Up/Down change the throw at the cursor, Ctrl+T (Cmd+T on a
            // Mac) swaps where two throws land (the two selected, or the two before the cursor),
            // and Ctrl+R / Ctrl+L rotate the pattern.
            siteswapBox.before = siteswapText;
            siteswapBox.swapRequested = siteswapBox.wasActive && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false);
            // Ctrl+R / Ctrl+L rotate the pattern right or left (the same pattern, started a
            // beat earlier or later). These repeat while held.
            siteswapBox.rotateRequested = 0;
            if (siteswapBox.wasActive && io.KeyCtrl && !io.KeyShift) {
                if (ImGui::IsKeyPressed(ImGuiKey_R)) siteswapBox.rotateRequested = 1;
                else if (ImGui::IsKeyPressed(ImGuiKey_L)) siteswapBox.rotateRequested = -1;
            }
            const ImGuiInputTextFlags boxFlags = ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackHistory |
                                                 ImGuiInputTextFlags_CallbackAlways;
            bool boxChanged = ImGui::InputText("##siteswap", siteswapText, sizeof(siteswapText), boxFlags,
                                               siteswapBoxCallback, &siteswapBox);
            const ImGuiID boxId = ImGui::GetItemID();
            const ImVec2 boxMin = ImGui::GetItemRectMin();
            const ImVec2 boxMax = ImGui::GetItemRectMax();
            const bool boxHovered = ImGui::IsItemHovered();
            siteswapBox.wasActive = ImGui::IsItemActive();
            if (!siteswapBox.wasActive) siteswapBox.memory.forget();
            // Typing is one undo step, recorded when you press Enter or leave the box (so
            // undoing "531" doesn't step back through "53" and "5"). The text is tidied then
            // ("<3p3|3p 3" becomes "<3p 3|3p 3>").
            const bool boxDone = ImGui::IsItemDeactivatedAfterEdit();
            if (boxDone) {
                const std::string tidy = tidySiteswap(siteswapText);
                if (tidy != siteswapText && tidy.size() < sizeof(siteswapText)) {
                    std::snprintf(siteswapText, sizeof(siteswapText), "%s", tidy.c_str());
                    boxChanged = true;
                }
            }
            if (boxChanged) {
                cancelLadderEdit(ladderEdit);
                parsed = parseSiteswap(siteswapText);
                patternValid = parsed.valid;
                sketching = parsed.sketch;
                if (parsed.valid) pattern = patternFromSiteswap(parsed);
                if (parsed.sketch) sketchLoop = parsed.loop;
            }
            if (boxDone && (parsed.valid || parsed.sketch)) recordPattern(siteswapText);

            // Where the text is drawn (scrolled, while it's being edited and too long to fit).
            float textLeft = boxMin.x + ImGui::GetStyle().FramePadding.x;
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(boxId)) textLeft -= state->Scroll.x;
            const float textBottom = boxMax.y - ImGui::GetStyle().FramePadding.y + 1.0f;
            ImDrawList* boxDraw = ImGui::GetWindowDrawList();
            boxDraw->PushClipRect(boxMin, boxMax, true);
            // Throws that are the problem (two landing together): a zigzag underline, in the
            // error color, like a spelling mistake.
            for (const Siteswap::ThrowRef& r : parsed.problemThrows) {
                int start = 0, end = 0;
                if (!charactersOfThrow(siteswapText, r.juggler, r.beat, &start, &end)) continue;
                const float x0 = siteswapCharacterX(siteswapText, start, textLeft);
                const float x1 = siteswapCharacterX(siteswapText, end, textLeft);
                const float step = 2.5f;
                ImVec2 points[64];
                int count = 0;
                for (float x = x0; x <= x1 + 0.01f && count < 64; x += step, ++count)
                    points[count] = ImVec2(std::min(x, x1), textBottom - ((count % 2) ? 2.5f : 0.0f));
                boxDraw->AddPolyline(points, count, errorTextColor(settings.colorVision), 0, 1.5f);
            }
            // Hovering a throw describes it and picks it out on the ladder.
            siteswapHoverThrow = -1;
            const Siteswap& shown = parsed;
            if (boxHovered && !shown.loop.empty()) {
                const float mx = io.MousePos.x;
                const int length = static_cast<int>(std::strlen(siteswapText));
                int index = -1;
                for (int i = 0; i < length; ++i) {
                    if (mx >= siteswapCharacterX(siteswapText, i, textLeft) &&
                        mx < siteswapCharacterX(siteswapText, i + 1, textLeft)) {
                        index = i;
                        break;
                    }
                }
                int juggler = 0, beat = 0;
                if (index >= 0 && throwAtCharacter(siteswapText, index, &juggler, &beat) &&
                    juggler < shown.loop.jugglers && beat < shown.loop.period) {
                    int start = 0, end = 0;
                    if (charactersOfThrow(siteswapText, juggler, beat, &start, &end))
                        boxDraw->AddLine(ImVec2(siteswapCharacterX(siteswapText, start, textLeft), textBottom + 1.0f),
                                         ImVec2(siteswapCharacterX(siteswapText, end, textLeft), textBottom + 1.0f),
                                         ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
                    if (shown.valid || shown.sketch) siteswapHoverThrow = juggler * shown.loop.period + beat;
                    ImGui::SetTooltip("%s", describeThrow(shown.loop, juggler, beat).c_str());
                }
            }
            boxDraw->PopClipRect();

            if (!siteswapBox.note.empty() && ImGui::GetTime() < siteswapBox.noteUntil)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)), "%s",
                                   siteswapBox.note.c_str());
            else if (patternValid && pattern.jugglers > 1)
                ImGui::TextDisabled("%d jugglers, %d objects, period %d", pattern.jugglers, ballCount(pattern),
                                    loopPeriodBeats(pattern));
            else if (patternValid)
                ImGui::TextDisabled("%d objects, period %d", ballCount(pattern),
                                    loopPeriodBeats(pattern));
            else if (sketching)
                ImGui::TextDisabled("Sketch: %d throw%s not decided yet (?), period %d", parsed.openThrows,
                                    parsed.openThrows == 1 ? "" : "s", sketchLoop.period);
            else if (!parsed.error.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "%s", parsed.error.c_str());
            else
                ImGui::TextUnformatted("");
        }
        ImGui::End();

        // --- The scene at this moment ---
        // The jugglers juggle the pattern, or the sketch as far as it goes (props pop in and
        // vanish where throws aren't decided yet), or, while drawing, what a click would make.
        const bool showJugglers = patternValid || sketching;
        const JugglingLoop loop = ladderPreview ? ladderPreviewLoop
                                                : (sketching ? sketchLoop : (patternValid ? patternLoop(pattern) : JugglingLoop()));
        const bool loopComplete = !loop.empty() && openThrowCount(loop) == 0;
        if (selectedJuggler >= std::max(loop.jugglers, pattern.jugglers) || std::max(loop.jugglers, pattern.jugglers) < 2) {
            selectedJuggler = -1;
            frameSelected = false;
        }
        JugglerScene scene;
        std::vector<int> styleOfBall;  // with "color by orbit": each prop's orbit
        if (showJugglers) {
            const BallOrbits ballOrbits = loopComplete ? computeBallOrbits(loop) : BallOrbits();
            scene = evaluateScene(loop, ballOrbits, juggleParams, playback.beat);
            if (settings.colorByOrbit && loopComplete)
                styleOfBall = orbitOfEachBall(loop, ballOrbits, computeLoopOrbits(loop));
            const int framedJuggler = frameSelected ? selectedJuggler : -1;
            if (loop != extentsLoop || juggleParams.bpm != extentsParams.bpm ||
                juggleParams.dwellBeats != extentsParams.dwellBeats ||
                juggleParams.prop != extentsParams.prop || juggleParams.distance != extentsParams.distance ||
                framedJuggler != extentsJuggler) {
                extents = computeSceneExtents(loop, juggleParams, framedJuggler);
                extentsLoop = loop;
                extentsParams = juggleParams;
                extentsJuggler = framedJuggler;
            }
        }

        // Mouse input for the 3D view: an invisible window over the juggler pane (the 3D view
        // itself is drawn straight to OpenGL, not by ImGui). Drag to orbit, wheel to zoom; with
        // passing patterns, click a juggler to select them (click empty space to deselect) and
        // double-click to frame them (double-click empty space to frame everyone).
        const float transportHeightForInput = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        const ImVec2 viewMin(leftWidth, top);
        const ImVec2 viewSize(W - leftWidth, H - top - transportHeightForInput);
        ImGui::SetNextWindowPos(viewMin);
        ImGui::SetNextWindowSize(viewSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Juggler view input", nullptr,
                         paneFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 1.0f && avail.y > 1.0f) {
                ImGui::InvisibleButton("##juggler_view", avail);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    const ImVec2 d = io.MouseDelta;
                    camera.yaw -= d.x * 0.008f;
                    camera.pitch = std::clamp(camera.pitch + d.y * 0.008f, -1.45f, 1.45f);
                }
                const bool canSelect = scene.jugglers.size() > 1;
                const int underMouse =
                    canSelect && ImGui::IsItemHovered() ? pickJuggler(scene, lastViewProj, viewMin, viewSize, io.MousePos)
                                                        : -1;
                // A click is a press and release without dragging (dragging orbits).
                const float clickSlop = io.MouseDragThreshold;
                if (canSelect && ImGui::IsItemDeactivated() &&
                    io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < clickSlop * clickSlop) {
                    selectedJuggler = underMouse;
                    if (underMouse < 0) frameSelected = false;
                }
                if (canSelect && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selectedJuggler = underMouse;
                    frameSelected = underMouse >= 0;
                }
                if (ImGui::IsItemHovered()) {
                    if (io.MouseWheel != 0.0f)
                        camera.zoom = std::clamp(camera.zoom * std::pow(0.88f, io.MouseWheel), 0.15f, 6.0f);
                    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home)) {
                        resetCamera(camera);
                        frameSelected = false;
                    }
                    // A hint, only after the mouse has rested here a moment.
                    if (!ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        if (underMouse >= 0)
                            ImGui::SetTooltip("Juggler %d\nClick to select, double-click to frame", underMouse + 1);
                        else if (canSelect)
                            ImGui::SetTooltip("Drag to orbit, wheel to zoom, Home to reset\n"
                                              "Click a juggler to select them, double-click to frame them");
                        else
                            ImGui::SetTooltip("Drag to orbit, wheel to zoom, Home to reset");
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
            ImGui::SetNextWindowSize(ImVec2(grab * 2.0f, H - top));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::Begin("##divider", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::InvisibleButton("##split", ImVec2(grab * 2.0f, H - top));
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
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(leftWidth, top), ImVec2(leftWidth, H),
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
        if (drawTweakablesPanel(juggleParams, loop, settings.tempoPanelCollapsed,
                                ImVec2(W - panelMargin, top + panelMargin), tweakablesPanel))
            saveSettings(settings);

        // Transport bar along the bottom of the juggler pane (right half).
        const float transportHeight = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(leftWidth, H - transportHeight));
        ImGui::SetNextWindowSize(ImVec2(W - leftWidth, transportHeight));
        if (ImGui::Begin("Transport", nullptr, paneFlags)) {
            const float h = ImGui::GetFrameHeight();
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
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Beat %.2f", playback.beat + 1.0);  // beat 0 is "beat 1", as on the ladder
        }
        ImGui::End();

        if (showColorPreview) drawColorVisionPreview(&showColorPreview, settings.colorVision);

        // Pattern library dialogs.
        const SavePatternRequest saveRequest =
            drawSavePatternDialog(saveDialog, siteswapText, currentPatternSettings(), myPatterns);
        if (saveRequest.save && (patternValid || sketching)) {
            LibraryPattern saved;
            saved.siteswap = siteswapText;
            saved.name = saveRequest.name;
            saved.settings = currentPatternSettings();
            saved.settings.hasDistance = (sketching ? sketchLoop.jugglers : pattern.jugglers) > 1;  // passing only
            classifyPattern(&saved);
            bool replaced = false;
            if (saveRequest.replace) {
                for (LibraryPattern& existing : myPatterns) {
                    if (existing.displayName() == saved.name) {
                        existing = saved;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced) myPatterns.push_back(saved);
            saveMyPatterns();
            savedPatternText = siteswapText;
            if (newAfterSave) newPatternDialog.openRequested = true;
            newAfterSave = false;
        } else if (newAfterSave && !saveDialog.openRequested && !ImGui::IsPopupOpen("Save to My Patterns")) {
            newAfterSave = false;  // the save was cancelled: so is the new pattern
        }
        const ManagePatternsRequest manageRequest =
            drawManagePatternsWindow(manageWindow, myPatterns, settings.colorVision);
        if (manageRequest.load >= 0) {
            const LibraryPattern chosen = myPatterns[static_cast<size_t>(manageRequest.load)];
            loadLibraryPattern(chosen, true);
        }
        if (manageRequest.rename >= 0) {
            myPatterns[static_cast<size_t>(manageRequest.rename)].name = manageRequest.newName;
            saveMyPatterns();
        }
        if (manageRequest.remove >= 0) {
            myPatterns.erase(myPatterns.begin() + manageRequest.remove);
            saveMyPatterns();
        }
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

        // File > New Pattern..., first offering to save the current pattern if it has changed.
        if (savePromptRequested) {
            ImGui::OpenPopup("Save changes?");
            savePromptRequested = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const float em = ImGui::GetFontSize();
            ImGui::Text("Save %s to My Patterns before starting a new pattern?", siteswapText);
            ImGui::Spacing();
            if (ImGui::Button("Save...", ImVec2(em * 7.0f, 0.0f))) {
                std::snprintf(saveDialog.name, sizeof(saveDialog.name), "%s", siteswapText);
                saveDialog.openRequested = true;
                newAfterSave = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(em * 7.0f, 0.0f))) {
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
            ImGui::TextUnformatted("Starts a blank ladder: every throw is \"?\" (not decided yet). "
                                   "Click a ? to draw a throw from it, then where it's caught, and so on.");
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
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(em * 6.0f, 0.0f))) {
                JugglingLoop blank;
                blank.jugglers = newPatternDialog.jugglers;
                blank.period = newPatternDialog.period;
                for (int j = 0; j < blank.jugglers; ++j)
                    for (int b = 0; b < blank.period; ++b) blank.throws.push_back({kOpenThrow, j});
                std::string text;
                if (loopToText(blank, &text)) {
                    commitTyping();
                    recordSettingsChange();
                    showPatternText(text);
                    recordPattern(text);
                    savedPatternText = text;
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
        const CameraView cameraView = updateCamera(camera, extents, jugglerAspect, io.DeltaTime);
        lastViewProj = cameraViewProj(cameraView, jugglerAspect);
        drawJugglerLabels(ImGui::GetBackgroundDrawList(), scene, lastViewProj, viewMin, viewSize, selectedJuggler);
        if (settings.showThrowValues)
            drawPropValueLabels(ImGui::GetBackgroundDrawList(), scene, lastViewProj, viewMin, viewSize,
                                settings.colorVision, styleOfBall, parsed.passStyle.relative);
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

        // Right half, below the menu bar. GL's origin is bottom-left.
        const int viewX = static_cast<int>(std::lround(leftWidth * pixelsPerPointX));
        const int transportPixels = static_cast<int>(std::lround(transportHeight * pixelsPerPointY));
        const int topPixels = static_cast<int>(std::lround(top * pixelsPerPointY));
        const GLRect jugglerRect{viewX, transportPixels, framebufferWidth - viewX,
                                 framebufferHeight - topPixels - transportPixels};
        renderJugglerView(renderer, prims, propMeshes, scene, cameraView, selectedJuggler, settings.colorVision, styleOfBall,
                          jugglerRect);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        platformSwapBuffers();
    }

    propMeshes.destroy();
    prims.destroy();
    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    return 0;
}
