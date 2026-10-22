// preferences_window.cpp - see preferences_window.h.

#include "preferences_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// A reading shown on a curve: an axis's name, its color, and its reading (% of its full push).
struct LiveReading {
    const char* name;
    ImU32 color;
    float percent;
};

// The axes, in the order of AppSettings::spaceFullPush.
const char* const kAxisNames[6] = {"sideways", "in/out", "up/down", "tilt", "roll", "spin"};
// A calibration that pushed an axis less than this keeps that axis's old full push (it wasn't
// pushed, or barely).
const float kMinFullPush = 20.0f;
float axisReading(const SpaceMouseState& m, int axis) { return axis < 3 ? m.move[axis] : m.turn[axis - 3]; }

// A round number of units for grid lines, about an eighth of `range`.
float gridStep(float range) {
    const float rough = range / 8.0f;
    const float power = std::pow(10.0f, std::floor(std::log10(std::max(rough, 1e-6f))));
    for (float m : {1.0f, 2.0f, 5.0f, 10.0f})
        if (rough <= m * power) return m * power;
    return 10.0f * power;
}

// One curve: its graph (readings across, rates up), its points to drag, and the live readings on
// it. `index` tells the curves apart for dragging. Returns true when an edit is finished.
// Across: 0-100% of full push.
bool curveEditor(int index, const char* title, const char* unit, ResponseCurve& curve,
                 const ResponseCurve& defaults, float& top, PreferencesWindowState& state,
                 const std::vector<LiveReading>& live, ImVec2 size) {
    const float right = 100.0f;
    bool finished = false;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::PushID(index);
    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        curve = defaults;
        top = 0.0f;
        finished = true;
    }
    // The Y axis goes a little above the highest point (held while a point is dragged, so the
    // graph doesn't move under the mouse).
    const float highest = *std::max_element(curve.y, curve.y + ResponseCurve::kPoints);
    if (state.dragCurve != index || top <= 0.0f) top = std::max(highest * 1.15f, 1.0f);

    const float em = ImGui::GetFontSize();
    const ImVec2 pad(em * 3.2f, em * 0.6f);  // room for the labels: left, and top
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 plotMin(origin.x + pad.x, origin.y + pad.y);
    const ImVec2 plotMax(origin.x + size.x - em * 0.6f, origin.y + size.y - em * 1.6f);
    ImGui::InvisibleButton("##graph", size);
    const bool hovered = ImGui::IsItemHovered();
    auto toScreen = [&](float x, float y) {
        return ImVec2(plotMin.x + (plotMax.x - plotMin.x) * x / right, plotMax.y - (plotMax.y - plotMin.y) * y / top);
    };
    auto fromScreen = [&](ImVec2 p, float* x, float* y) {
        *x = (p.x - plotMin.x) / (plotMax.x - plotMin.x) * right;
        *y = (plotMax.y - p.y) / (plotMax.y - plotMin.y) * top;
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(plotMin, plotMax, IM_COL32(24, 25, 30, 255));
    const ImU32 gridColor = IM_COL32(70, 72, 80, 255), labelColor = IM_COL32(150, 152, 160, 255);
    const float xStep = gridStep(right), yStep = gridStep(top);
    for (float x = 0.0f; x <= right + 0.5f; x += xStep) {
        const ImVec2 a = toScreen(x, 0.0f), b = toScreen(x, top);
        dl->AddLine(ImVec2(a.x, b.y), a, gridColor);
        char text[16];
        std::snprintf(text, sizeof(text), "%g", x);
        dl->AddText(ImVec2(a.x - ImGui::CalcTextSize(text).x * 0.5f, plotMax.y + 2.0f), labelColor, text);
    }
    for (float y = 0.0f; y <= top + 1e-3f; y += yStep) {
        const ImVec2 a = toScreen(0.0f, y), b = toScreen(right, y);
        dl->AddLine(a, b, gridColor);
        char text[16];
        std::snprintf(text, sizeof(text), "%g", y);
        dl->AddText(ImVec2(plotMin.x - ImGui::CalcTextSize(text).x - em * 0.3f, a.y - em * 0.5f), labelColor, text);
    }
    dl->AddRect(plotMin, plotMax, IM_COL32(110, 112, 120, 255));

    // The curve: nothing until the first point (the dead zone), the last point's rate after the last.
    std::vector<ImVec2> line;
    const int samples = static_cast<int>(plotMax.x - plotMin.x);
    for (int i = 0; i <= samples; ++i) {
        const float x = right * static_cast<float>(i) / static_cast<float>(std::max(samples, 1));
        line.push_back(toScreen(x, std::min(responseRate(curve, x), top)));
    }
    dl->AddPolyline(line.data(), static_cast<int>(line.size()), IM_COL32(120, 190, 255, 255), 0, 2.0f);

    // The points: drag one (between its neighbors).
    const float grab = em * 0.6f;
    int hot = -1;
    for (int i = 0; i < ResponseCurve::kPoints; ++i) {
        const ImVec2 p = toScreen(curve.x[i], curve.y[i]);
        if (hovered && std::hypot(io.MousePos.x - p.x, io.MousePos.y - p.y) <= grab) hot = i;
    }
    if (hot >= 0 && ImGui::IsItemActivated()) {
        state.dragCurve = index;
        state.dragPoint = hot;
    }
    if (state.dragCurve == index && state.dragPoint >= 0) {
        if (ImGui::IsItemActive()) {
            const int i = state.dragPoint;
            float x = 0.0f, y = 0.0f;
            fromScreen(io.MousePos, &x, &y);
            // (Placed to a tenth: finer than the readings and rates themselves, but it shapes the curve.)
            const float lo = i == 0 ? 0.0f : curve.x[i - 1] + 0.1f;
            const float hi = i == ResponseCurve::kPoints - 1 ? right : curve.x[i + 1] - 0.1f;
            curve.x[i] = std::round(std::clamp(x, lo, hi) * 10.0f) / 10.0f;
            curve.y[i] = std::clamp(y, 0.0f, top);
            curve.y[i] = std::round(curve.y[i] * 10.0f) / 10.0f;
        } else {
            state.dragCurve = state.dragPoint = -1;
            finished = true;
        }
    }
    for (int i = 0; i < ResponseCurve::kPoints; ++i) {
        const ImVec2 p = toScreen(curve.x[i], curve.y[i]);
        const bool lit = i == hot || (state.dragCurve == index && state.dragPoint == i);
        dl->AddCircleFilled(p, lit ? em * 0.42f : em * 0.32f, lit ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 225, 255, 255));
        dl->AddCircle(p, lit ? em * 0.42f : em * 0.32f, IM_COL32(20, 20, 24, 255), 0, 1.5f);
    }
    if (hot >= 0 || (state.dragCurve == index && state.dragPoint >= 0)) {
        const int i = hot >= 0 ? hot : state.dragPoint;
        ImGui::SetTooltip("%g%%: %g %s", curve.x[i], curve.y[i], unit);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Each axis's reading now, and what it gives.
    for (const LiveReading& r : live) {
        if (r.percent <= 0.0f) continue;
        const float rate = responseRate(curve, r.percent);
        const float x = std::min(r.percent, right);
        const ImVec2 a = toScreen(x, 0.0f), b = toScreen(x, top);
        dl->AddLine(a, b, (r.color & 0x00FFFFFFu) | 0x60000000u, 1.0f);
        dl->AddCircleFilled(toScreen(x, std::min(rate, top)), em * 0.28f, r.color);
    }
    std::string readout;
    for (const LiveReading& r : live) {
        char text[96];
        std::snprintf(text, sizeof(text), "%s%s %d%%: %.0f", readout.empty() ? "" : "   ", r.name,
                      static_cast<int>(std::lround(r.percent)), responseRate(curve, r.percent));
        readout += text;
    }
    ImGui::TextDisabled("%s  (%s)", readout.c_str(), unit);
    ImGui::PopID();
    return finished;
}

}  // namespace

bool drawPreferencesWindow(bool* open, PreferencesWindowState& state, AppSettings& settings,
                           const SpaceMouseState& spaceMouse, ImVec2 pos, ImVec2 size) {
    bool finished = false;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowBgAlpha(1.0f);  // (opaque: the ladder under it would only confuse the graphs)
    if (ImGui::Begin("Preferences", open,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::IsWindowAppearing()) state.calibrating = false;  // (closed mid-calibration: start over)
        ImGui::SeparatorText("3D mouse");
        ImGui::TextWrapped(
            "How hard the cap is pushed (across, as a percentage of that axis's full push) gives how fast the camera "
            "moves (up). Nothing happens until the first point; past the last, the last point's rate. Drag the "
            "points, and try the cap as you go: each axis's reading shows on its curve.");
        if (!spaceMouse.present) ImGui::TextDisabled("(No 3D mouse found.)");

        // Full push: each axis's hardest push, measured by Calibrate.
        if (!state.calibrating) {
            if (ImGui::Button("Calibrate...")) {
                state.calibrating = true;
                for (float& m : state.calibrationMax) m = 0.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Full push: %s %.0f, %s %.0f, %s %.0f, %s %.0f, %s %.0f", kAxisNames[0],
                                settings.spaceFullPush[0], kAxisNames[1], settings.spaceFullPush[1], kAxisNames[2],
                                settings.spaceFullPush[2], kAxisNames[3], settings.spaceFullPush[3], kAxisNames[5],
                                settings.spaceFullPush[5]);
        } else {
            for (int a = 0; a < 6; ++a)
                state.calibrationMax[a] = std::max(state.calibrationMax[a], std::fabs(axisReading(spaceMouse, a)));
            ImGui::TextWrapped(
                "Push the cap each way, as hard as you'd normally push it when working: sideways, in and out, up and "
                "down, tilt it toward and away from you, and spin it both ways. Then Done.");
            for (int a = 0; a < 6; ++a) {
                if (a == 4) continue;  // (roll isn't used)
                const bool enough = state.calibrationMax[a] >= kMinFullPush;
                ImGui::Text("  %-9s", kAxisNames[a]);
                ImGui::SameLine();
                if (enough)
                    ImGui::Text("%.0f", state.calibrationMax[a]);
                else
                    ImGui::TextDisabled("%.0f  (not pushed yet: keeps %.0f)", state.calibrationMax[a],
                                        settings.spaceFullPush[a]);
            }
            if (ImGui::Button("Done")) {
                for (int a = 0; a < 6; ++a)
                    if (state.calibrationMax[a] >= kMinFullPush) settings.spaceFullPush[a] = state.calibrationMax[a];
                state.calibrating = false;
                finished = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) state.calibrating = false;
        }

        const ImU32 kX = IM_COL32(240, 110, 100, 255), kY = IM_COL32(120, 220, 120, 255),
                    kZ = IM_COL32(110, 170, 255, 255);
        auto percent = [&](int axis) {
            return std::fabs(axisReading(spaceMouse, axis)) * 100.0f / std::max(settings.spaceFullPush[axis], 1.0f);
        };
        const std::vector<LiveReading> slides = {
            {"sideways", kX, percent(0)}, {"in/out", kY, percent(1)}, {"up/down", kZ, percent(2)}};
        const std::vector<LiveReading> turns = {{"tilt", kX, percent(3)}, {"spin", kZ, percent(5)}};
        const float em = ImGui::GetFontSize();
        const float graphHeight = std::max(em * 8.0f, (ImGui::GetContentRegionAvail().y - em * 6.0f) * 0.5f);
        const ImVec2 graph(ImGui::GetContentRegionAvail().x, graphHeight);
        finished |= curveEditor(0, "Sliding (pixels a second, at the orbit point)", "px/s", settings.spaceSlideCurve,
                                defaultSlideCurve(), state.slideTop, state, slides, graph);
        ImGui::Spacing();
        finished |= curveEditor(1, "Turning (degrees a second)", "deg/s", settings.spaceTurnCurve, defaultTurnCurve(),
                                state.turnTop, state, turns, graph);
    }
    ImGui::End();
    return finished;
}
