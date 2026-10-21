// panels.cpp - see panels.h.

#include "panels.h"

#include "stage_overlays.h"
#include "imgui_internal.h"  // to open a slider's text field on double-click (fineSlider)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr double kMinTempo = 40.0, kMaxTempo = 420.0;  // 420 BPM: 7 throws a second
constexpr double kMinDwell = 0.1, kMaxDwell = 1.9;

}  // namespace

namespace {

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

}  // namespace

void resetTweakables(JuggleParams& params) {
    const JuggleParams defaults;
    params.bpm = defaults.bpm;
    params.dwellBeats = defaults.dwellBeats;
    params.distance = defaults.distance;
}

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
                ImGui::SetTooltip(distanceLocked ? "150 BPM, dwell 1.60 beats (the distance stays: the pattern has choreography)"
                                                 : "150 BPM, dwell 1.60 beats, automatic distance");

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

bool turnEditor(const char* id, Turn* turn, Turn* hovered) {
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
