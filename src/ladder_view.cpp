// ladder_view.cpp - see ladder_view.h.
#include "ladder_view.h"

#include "draw_helpers.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Musical repeat signs, ||: :||, drawn into the given rectangle.
void drawRepeatSignGlyph(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color) {
    const float h = max.y - min.y;
    const float w = max.x - min.x;
    const float top = min.y + h * 0.2f;
    const float bottom = max.y - h * 0.2f;
    const float midY = (min.y + max.y) * 0.5f;
    const float thick = std::max(2.0f, h * 0.12f);
    const float thin = std::max(1.0f, h * 0.05f);
    const float barGap = h * 0.10f;
    const float dotGap = h * 0.14f;
    const float dotRadius = std::max(1.2f, h * 0.065f);
    const float dotOffset = h * 0.14f;

    for (int side = 0; side < 2; ++side) {
        // side 0 = opening sign at the left edge; side 1 = closing sign mirrored at the right.
        const float dir = side == 0 ? 1.0f : -1.0f;
        float x = side == 0 ? min.x + w * 0.12f : max.x - w * 0.12f;
        dl->AddRectFilled(ImVec2(std::min(x, x + dir * thick), top),
                          ImVec2(std::max(x, x + dir * thick), bottom), color);
        x += dir * (thick + barGap);
        dl->AddRectFilled(ImVec2(std::min(x, x + dir * thin), top),
                          ImVec2(std::max(x, x + dir * thin), bottom), color);
        x += dir * (thin + dotGap + dotRadius);
        dl->AddCircleFilled(ImVec2(x, midY - dotOffset), dotRadius, color);
        dl->AddCircleFilled(ImVec2(x, midY + dotOffset), dotRadius, color);
    }
}

}  // namespace

LadderToolbarRequest drawLadderToolbar(const Pattern& pattern, bool patternValid) {
    LadderToolbarRequest request;
    const float h = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();

    // Editing-mode toggle. Pattern mode is the only mode so far, so it's shown pressed in.
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::Button("##edit_mode", ImVec2(h * 2.2f, h));
    ImGui::PopStyleColor();
    drawRepeatSignGlyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                        ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Text));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Pattern/Sequence Mode\n\n"
            "Currently: Pattern mode. The pattern is one loop that repeats forever,\n"
            "and edits apply to every repeat.\n\n"
            "Sequence mode (a timeline with exact repeat counts) isn't implemented yet.");

    // Period control: write the loop out at a multiple of its shortest period.
    const int period = patternValid ? loopPeriodBeats(pattern) : 0;
    const int shortest = patternValid ? shortestPeriodBeats(pattern) : 0;
    const char* kPeriodHelp =
        "Loop length in beats.\n\n"
        "+ writes the loop out longer so the copies can be edited separately:\n"
        "531 at period 6 is 531531. - shortens it again.\n"
        "Only multiples of the shortest period are possible.";

    ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Period");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kPeriodHelp);

    ImGui::SameLine();
    ImGui::BeginDisabled(!patternValid || period <= shortest);
    if (ImGui::Button("-##period", ImVec2(h, h))) request.newPeriodBeats = period - shortest;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", kPeriodHelp);

    ImGui::SameLine();
    if (patternValid)
        ImGui::Text("%d", period);
    else
        ImGui::TextDisabled("-");

    ImGui::SameLine();
    ImGui::BeginDisabled(!patternValid || period + shortest > kMaxLoopPeriodBeats);
    if (ImGui::Button("+##period", ImVec2(h, h))) request.newPeriodBeats = period + shortest;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", kPeriodHelp);

    return request;
}

void drawLadderDiagram(const Pattern& pattern, bool patternValid, ColorVisionMode colorVision) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 60.0f || size.y < 60.0f) return;

    // Reserve the canvas area (and, later, receive mouse input for editing).
    ImGui::InvisibleButton("##ladder_canvas", size);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 maxPt(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, maxPt, true);
    dl->AddRectFilled(origin, maxPt, kCanvasBackground);

    const float fontSize = ImGui::GetFontSize();
    const float margin = fontSize;
    // Vertical layout, top to bottom: header + legend, room for the right hand's same-hand
    // (even) arches, the ladder itself (odd throws cross between the rails), room for the left
    // hand's arches, then beat numbers.
    const float headerHeight = margin * 0.5f + fontSize * 2.8f;
    const float numbersHeight = fontSize * 1.8f;
    const float bodyHeight = size.y - headerHeight - numbersHeight;
    const float outsideRoom = bodyHeight * 0.28f;  // above the top rail and below the bottom one
    const float railTopY = origin.y + headerHeight + outsideRoom;
    const float railBotY = origin.y + headerHeight + bodyHeight - outsideRoom;
    const float x0 = origin.x + margin * 3.0f;  // first beat; room for the R/L labels
    const float beatSpacing = fontSize * 3.0f;
    const int beats = std::max(1, static_cast<int>((maxPt.x - margin - x0) / beatSpacing) + 1);

    auto beatX = [&](int b) { return x0 + static_cast<float>(b) * beatSpacing; };
    auto railY = [&](int b) { return (b % 2 == 0) ? railTopY : railBotY; };  // beat 0 = right hand

    const ImU32 railCol = IM_COL32(150, 155, 170, 255);
    const ImU32 rungRightCol = IM_COL32(110, 116, 132, 255);
    const ImU32 rungLeftCol = IM_COL32(58, 62, 74, 255);
    const ImU32 textCol = IM_COL32(150, 155, 170, 255);
    const ImU32 dimText = IM_COL32(100, 104, 116, 255);

    const std::vector<int> loop = patternValid ? loopThrowValues(pattern) : std::vector<int>();

    // Background: alternate shades every loop period, so the repeat stands out. Plain grays,
    // independent of color-vision mode.
    if (!loop.empty()) {
        const int loopBeats = static_cast<int>(loop.size());
        // Kept very dark so the throw colors keep as much contrast as possible.
        const ImU32 periodShades[2] = {kCanvasBackground, IM_COL32(24, 26, 32, 255)};
        const float top = origin.y + headerHeight;
        for (int b = 0; b < beats; ++b) {
            const float left = beatX(b) - beatSpacing * 0.5f;
            dl->AddRectFilled(ImVec2(left, top), ImVec2(left + beatSpacing, maxPt.y),
                              periodShades[(b / loopBeats) % 2]);
        }
    }

    // Rungs, rails, labels, and beat numbers.
    for (int b = 0; b < beats; ++b) {
        // Beat line: from the top of the right hand's arch space down to just above the beat
        // number, so any point on a throw can be traced straight down to its beat.
        // Right-hand beats (1, 3, 5, ...) get a brighter, heavier line than left-hand beats, so
        // a throw's hand can be read from the line it starts on.
        const bool rightHandBeat = (b % 2 == 0);
        dl->AddLine(ImVec2(beatX(b), railTopY - outsideRoom),
                    ImVec2(beatX(b), maxPt.y - numbersHeight + fontSize * 0.15f),
                    rightHandBeat ? rungRightCol : rungLeftCol, rightHandBeat ? 2.0f : 1.0f);
        std::string label = std::to_string(b + 1);
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(beatX(b) - ts.x * 0.5f, maxPt.y - numbersHeight + fontSize * 0.3f),
                    dimText, label.c_str());
    }
    const float railStart = x0 - margin;
    dl->AddLine(ImVec2(railStart, railTopY), ImVec2(maxPt.x, railTopY), railCol, 2.0f);
    dl->AddLine(ImVec2(railStart, railBotY), ImVec2(maxPt.x, railBotY), railCol, 2.0f);
    dl->AddText(ImVec2(origin.x + margin * 0.6f, railTopY - fontSize * 0.5f), textCol, "R");
    dl->AddText(ImVec2(origin.x + margin * 0.6f, railBotY - fontSize * 0.5f), textCol, "L");

    if (loop.empty()) {
        const char* msg = "No valid pattern to display. Enter a siteswap below.";
        dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f), dimText, msg);
        dl->PopClipRect();
        return;
    }

    const int period = static_cast<int>(loop.size());
    auto throwAt = [&](int beat) { return loop[static_cast<size_t>(beat % period)]; };  // beat >= 0

    std::string header = "Ladder diagram: " + std::to_string(ballCount(pattern)) +
                         " objects, period " + std::to_string(period);
    dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f), dimText, header.c_str());

    // Work out which ball each throw carries by following landings forward in time.
    std::vector<int> ballOfThrow(static_cast<size_t>(beats), -1);
    std::unordered_map<int, int> arriving;  // landing beat -> ball id
    int nextBall = 0;
    for (int b = 0; b < beats; ++b) {
        int t = throwAt(b);
        if (t == 0) continue;
        int ball;
        std::unordered_map<int, int>::iterator it = arriving.find(b);
        if (it != arriving.end()) {
            ball = it->second;
            arriving.erase(it);
        } else {
            ball = nextBall++;  // a ball entering the pattern
        }
        ballOfThrow[static_cast<size_t>(b)] = ball;
        arriving[b + t] = ball;
    }

    // Legend: one sample per ball, so identity never depends on color alone.
    const float thickness = 2.5f;
    const float arrowSize = fontSize * 0.55f;
    const float markerRadius = fontSize * 0.36f;
    const float dashUnit = fontSize * 0.35f;
    {
        float x = origin.x + margin;
        const float y = origin.y + margin * 0.5f + fontSize * 1.9f;
        const float sampleLength = fontSize * 2.6f;
        for (int ball = 0; ball < nextBall; ++ball) {
            const BallStyle style = ballStyle(colorVision, ball);
            const ImVec2 a(x + markerRadius, y);
            const ImVec2 b(x + markerRadius + sampleLength, y);
            drawStyledLine(dl, a, b, style.color, thickness, style.dash, dashUnit);
            drawMarker(dl, a, markerRadius, style.shape, style.color, 2.0f, kCanvasBackground);
            const std::string label = std::to_string(ball + 1);
            dl->AddText(ImVec2(b.x + fontSize * 0.3f, y - fontSize * 0.5f), textCol, label.c_str());
            x = b.x + fontSize * 0.3f + ImGui::CalcTextSize(label.c_str()).x + fontSize * 1.0f;
        }
    }

    // Pass 1: throw curves and arrowheads. Pass 2: markers on top, ringed in the background
    // color so they stay readable where curves cross them.
    for (int b = 0; b < beats; ++b) {
        const int t = throwAt(b);
        if (t == 0) continue;
        const BallStyle style = ballStyle(colorVision, ballOfThrow[static_cast<size_t>(b)]);
        const ImVec2 p0(beatX(b), railY(b));
        const ImVec2 p1(beatX(b + t), railY(b + t));
        const float dx = p1.x - p0.x;
        ImVec2 c1, c2;
        if (t % 2 == 1) {
            // Crossing throw: gentle S-curve from one rail to the other.
            c1 = ImVec2(p0.x + dx * 0.35f, p0.y);
            c2 = ImVec2(p1.x - dx * 0.35f, p1.y);
        } else {
            // Same-hand throw: arch outside the ladder (right hand above the top rail, left hand
            // below the bottom one), taller for higher throws, reaching the edge of the
            // available room at 8.
            const float bulge = outsideRoom * 0.9f * std::min(1.0f, static_cast<float>(t) / 8.0f);
            const float dir = (p0.y == railTopY) ? -1.0f : 1.0f;
            const float cy = p0.y + dir * bulge * (4.0f / 3.0f);  // cubic peak ~= bulge
            c1 = ImVec2(p0.x + dx * 0.1f, cy);
            c2 = ImVec2(p1.x - dx * 0.1f, cy);
        }
        drawStyledBezier(dl, p0, c1, c2, p1, style.color, thickness, style.dash, dashUnit);
        drawBezierArrowHead(dl, p0, c1, c2, p1, style.color, arrowSize, markerRadius + 3.0f);
    }
    for (int b = 0; b < beats; ++b) {
        const int t = throwAt(b);
        const ImVec2 p0(beatX(b), railY(b));
        if (t == 0) {
            dl->AddCircle(p0, fontSize * 0.25f, dimText, 0, 1.5f);  // empty hand
            continue;
        }
        const BallStyle style = ballStyle(colorVision, ballOfThrow[static_cast<size_t>(b)]);
        drawMarker(dl, p0, markerRadius, style.shape, style.color, 2.0f, kCanvasBackground);
    }

    dl->PopClipRect();
}
