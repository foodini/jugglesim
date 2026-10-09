// ladder_view.cpp - see ladder_view.h.
#include "ladder_view.h"

#include "draw_helpers.h"
#include "loop_ops.h"
#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>
#include <map>
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

// A small orbit: an ellipse with a prop on it.
void drawOrbitGlyph(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color) {
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float rx = (max.x - min.x) * 0.32f, ry = (max.y - min.y) * 0.26f;
    dl->PathClear();
    for (int i = 0; i <= 24; ++i) {
        const float a = static_cast<float>(i) / 24.0f * 6.2831853f;
        dl->PathLineTo(ImVec2(c.x + rx * std::cos(a), c.y + ry * std::sin(a)));
    }
    dl->PathStroke(color, 0, std::max(1.5f, (max.y - min.y) * 0.07f));
    dl->AddCircleFilled(ImVec2(c.x + rx * 0.7071f, c.y - ry * 0.7071f), std::max(2.0f, (max.y - min.y) * 0.12f), color);
}

bool allStripsCollapsed(const LadderEditState& edit, int jugglers) {
    for (int j = 0; j < jugglers && j < kMaxJugglers; ++j)
        if (!edit.collapsed[static_cast<size_t>(j)]) return false;
    return jugglers > 0;
}

void toggleCollapseAll(LadderEditState& edit, int jugglers) {
    const bool collapse = !allStripsCollapsed(edit, jugglers);
    for (bool& c : edit.collapsed) c = collapse;
}

// Two interlocking rings, for the dim-linked-jugglers button.
static void drawLinkGlyph(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color) {
    const float cx = 0.5f * (min.x + max.x), cy = 0.5f * (min.y + max.y);
    const float r = (max.y - min.y) * 0.2f;
    dl->AddCircle(ImVec2(cx - r * 0.7f, cy), r, color, 16, 1.6f);
    dl->AddCircle(ImVec2(cx + r * 0.7f, cy), r, color, 16, 1.6f);
}

// Arrows for the collapse/expand-all button: two triangles pointing at each other (collapse),
// or away from each other (expand).
static void drawCollapseGlyph(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, bool expand) {
    const float cx = 0.5f * (min.x + max.x), cy = 0.5f * (min.y + max.y);
    const float h = (max.y - min.y) * 0.22f;
    const float w = h * 1.1f;
    const float gap = h * 0.35f;
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const float base = cx + s * (gap + w), tip = cx + s * gap;  // pointing in
        if (expand)
            dl->AddTriangleFilled(ImVec2(cx + s * gap, cy - h), ImVec2(cx + s * gap, cy + h), ImVec2(cx + s * (gap + w), cy), color);
        else
            dl->AddTriangleFilled(ImVec2(base, cy - h), ImVec2(base, cy + h), ImVec2(tip, cy), color);
    }
}

LadderToolbarRequest drawLadderToolbar(const JugglingLoop& loop, const LadderEditState& edit, bool showValues,
                                       bool colorByOrbit, bool hasLinks, bool dimLinked) {
    const bool patternValid = !loop.empty();
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

    // Throw values: label every throw ("3", "4p"). Shown pressed in while on.
    ImGui::SameLine();
    if (showValues) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("3p##values", ImVec2(h * 1.6f, h))) request.toggleValues = true;
    if (showValues) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Throw values (V)\n\nLabels every throw with its value: 3, 4p (a pass), ...");

    // Color by orbit. Shown pressed in while on.
    ImGui::SameLine();
    if (colorByOrbit) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("##orbits", ImVec2(h * 1.6f, h))) request.toggleOrbits = true;
    if (colorByOrbit) ImGui::PopStyleColor();
    drawOrbitGlyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                   ImGui::GetColorU32(ImGuiCol_Text));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Color by orbit (O)\n\nOne color per orbit (the throws a group of props travels\n"
                          "round) instead of one per prop. Sketches are always colored by path.");

    // Collapse/expand all jugglers' strips (with several jugglers). The arrows show what a click
    // does: pointing in to collapse, out to expand.
    if (loop.jugglers > 1) {
        ImGui::SameLine();
        const bool expand = allStripsCollapsed(edit, loop.jugglers);
        if (ImGui::Button("##collapse_all", ImVec2(h * 1.6f, h))) request.toggleCollapseAll = true;
        drawCollapseGlyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                          ImGui::GetColorU32(ImGuiCol_Text), expand);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(expand ? "Expand all jugglers (C)\n\nEvery juggler's strip back to full width."
                                     : "Collapse all jugglers (C)\n\nEvery juggler's strip narrow (their throws still show).\n"
                                       "The triangle button left of each juggler's number collapses or expands just that one.");
    }

    // Dim linked jugglers (with links, "@2[3]"): their throws muted, so the parts written out
    // stand out. Shown pressed in while on.
    if (hasLinks) {
        ImGui::SameLine();
        if (dimLinked) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("##dim_linked", ImVec2(h * 1.6f, h))) request.toggleDimLinked = true;
        if (dimLinked) ImGui::PopStyleColor();
        drawLinkGlyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                      ImGui::GetColorU32(ImGuiCol_Text));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dim linked jugglers (L)\n\nMutes the throws of jugglers who are links (@2[3]: another\n"
                              "juggler's throws), so the parts written out stand out.");
    }

    // Period control: write the loop out at a multiple of its shortest period.
    const int period = loop.period;
    const int shortest = patternValid ? loopShortestPeriod(loop) : 0;
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
    ImGui::BeginDisabled(!patternValid || period + shortest > kMaxLoopBeats);
    if (ImGui::Button("+##period", ImVec2(h, h))) request.newPeriodBeats = period + shortest;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", kPeriodHelp);

    // Reset View: enabled whenever the ladder is panned or zoomed, and highlighted when beat 1
    // has gone off screen (it's easy to get lost).
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
    const bool atDefault = ladderViewIsDefault(edit);
    const bool lost = !edit.beatOneVisible && !atDefault;
    if (lost) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::BeginDisabled(atDefault);
    if (ImGui::Button("Reset View")) request.resetView = true;
    ImGui::EndDisabled();
    if (lost) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Back to the default pan and zoom, with beat 1 at the top (Home, or Ctrl+0).\n\n"
                          "On the ladder: mouse wheel scrolls, Ctrl+wheel zooms.");

    return request;
}

namespace {

// Geometry of one throw curve on the ladder.
struct Curve {
    ImVec2 p0, c1, c2, p1;
};

// A throw as drawn this frame (one instance of a loop slot).
struct DrawnThrow {
    Slot from;      // departure
    LoopThrow t;    // value and destination juggler
    int ball;       // ball id (index into the ball styles)
    Curve curve;
    Slot to() const { return landingSlot(from, t); }
};

// What the mouse is over: a throw end (idle) or a drop target (holding).
struct HitResult {
    int index = -1;  // index into the DrawnThrow list, or -1
    float t = 0.0f;  // curve parameter of the nearest point
    float distance = 1e9f;
};

char valueChar(int value) {
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

}  // namespace

std::string throwLabel(const LoopThrow& t, int thrower, int jugglers, bool relative) {
    std::string label(1, valueChar(t.value));
    if (t.dest != thrower) {
        label += 'p';
        if (relative && jugglers > 0)
            label += '+' + std::to_string(((t.dest - thrower) % jugglers + jugglers) % jugglers);
        else if (jugglers > 2)
            label += std::to_string(t.dest + 1);
    }
    return label;
}

void resetLadderView(LadderEditState& edit) {
    edit.firstBeat = 0.0f;
    edit.zoom = 1.0f;
}

bool ladderViewIsDefault(const LadderEditState& edit) {
    return edit.firstBeat == 0.0f && edit.zoom == 1.0f;
}

void cancelLadderEdit(LadderEditState& edit) {
    edit.chain = EditChain();
    edit.hovering = false;
    edit.targeting = false;
    edit.drawing = false;
    edit.pathHighlight.clear();
}

LadderEditResult drawLadderDiagram(const JugglingLoop& loop, ColorVisionMode colorVision, LadderEditState& edit,
                                   double playheadBeat, const LadderViewOptions& options) {
    LadderEditResult result;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 60.0f || size.y < 60.0f) return result;

    // The canvas is one invisible button: it reserves the area and receives mouse input.
    ImGui::InvisibleButton("##ladder_canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool leftClicked = canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool rightClicked = canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float time = static_cast<float>(ImGui::GetTime());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 maxPt(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, maxPt, true);
    dl->AddRectFilled(origin, maxPt, kCanvasBackground);

    if (loop.empty() && (edit.chain.active || edit.drawing)) cancelLadderEdit(edit);
    const bool sketch = !loop.empty() && openThrowCount(loop) > 0;
    if (sketch && edit.chain.active) cancelLadderEdit(edit);  // chains need a complete pattern
    EditChain& chain = edit.chain;
    const int period = loop.period;
    const int jugglers = std::max(1, loop.jugglers);
    const bool shiftHeld = ImGui::GetIO().KeyShift;

    // ---- Layout --------------------------------------------------------------------------------
    // Top to bottom: header + legend, the strip headers (juggler numbers with several jugglers,
    // then L and R over the hand columns), then the ladder itself, time running down. Beat
    // numbers are in a gutter on the left.
    //
    // Across each juggler's strip: room for the left hand's same-hand (even) arches, the left
    // hand's column, the space odd throws cross, the right hand's column, and room for the
    // right hand's arches. Same-hand throws bulge away from the middle of the juggler throwing
    // them, so the crossings stay between that juggler's columns. Passes run from strip to strip.
    const float fontSize = ImGui::GetFontSize();
    const float margin = fontSize;
    const float headerHeight = margin * 0.5f + fontSize * 2.8f;
    const float stripHeaderHeight = fontSize * (jugglers > 1 ? 2.9f : 1.5f);
    const float bodyTop = origin.y + headerHeight + stripHeaderHeight;
    const float bodyHeight = maxPt.y - bodyTop;
    const float gutterWidth = ImGui::CalcTextSize("-000").x + fontSize * 1.1f;
    const float areaLeft = origin.x + gutterWidth;
    const float areaWidth = std::max(1.0f, maxPt.x - margin * 0.5f - areaLeft);
    // Strips are kept apart by a gap (where the passes cross), shrinking when space is short.
    // A collapsed strip is narrow (the same layout, smaller); the others share what's left.
    const float jugglerCount = static_cast<float>(jugglers);
    auto isCollapsed = [&](int j) { return jugglers > 1 && j < kMaxJugglers && edit.collapsed[static_cast<size_t>(j)]; };
    // Jugglers who are links ("@2[3]"), and whether to mute their throws.
    const PatternForm* form = options.form && options.form->jugglers == jugglers ? options.form : nullptr;
    auto isLinked = [&](int j) { return form && form->linked(j); };
    auto muted = [&](int j, ImU32 color) {
        return options.dimLinked && isLinked(j) ? mixColor(color, kCanvasBackground, 0.65f, 0.75f) : color;
    };
    int collapsedCount = 0;
    for (int j = 0; j < jugglers; ++j)
        if (isCollapsed(j)) ++collapsedCount;
    const int expandedCount = jugglers - collapsedCount;
    const float collapsedWidth = fontSize * 4.6f;
    const float collapsedTotal = collapsedWidth * static_cast<float>(collapsedCount);
    const float stripGap =
        jugglers > 1 ? std::clamp((areaWidth - static_cast<float>(expandedCount) * fontSize * 12.0f - collapsedTotal) / (jugglerCount - 1.0f),
                                  0.0f, fontSize * 5.0f)
                     : 0.0f;
    const float stripWidth =
        expandedCount > 0 ? std::max(fontSize * 3.0f, std::min((areaWidth - stripGap * (jugglerCount - 1.0f) - collapsedTotal) /
                                                                   static_cast<float>(expandedCount),
                                                               fontSize * 22.0f))
                          : 0.0f;
    auto widthOf = [&](int j) { return isCollapsed(j) ? collapsedWidth : stripWidth; };
    float stripsWidth = stripGap * (jugglerCount - 1.0f);
    for (int j = 0; j < jugglers; ++j) stripsWidth += widthOf(j);
    const float stripsLeft = areaLeft + (areaWidth - stripsWidth) * 0.5f;
    const float stripsRight = stripsLeft + stripsWidth;
    std::vector<float> stripLefts(static_cast<size_t>(jugglers));
    for (int j = 0; j < jugglers; ++j) {
        stripLefts[static_cast<size_t>(j)] = j == 0 ? stripsLeft : stripLefts[static_cast<size_t>(j - 1)] + widthOf(j - 1) + stripGap;
    }
    auto stripLeft = [&](int j) { return stripLefts[static_cast<size_t>(j)]; };
    auto outsideRoomOf = [&](int j) { return widthOf(j) * 0.25f; };  // beside each column, for same-hand arches
    auto columnX = [&](int j, bool right) {
        return stripLeft(j) + outsideRoomOf(j) + (right ? widthOf(j) * 0.5f : 0.0f);
    };
    // About 24 beats on screen at zoom 1, within sensible limits for the markers.
    const float baseSpacing = std::clamp(bodyHeight / 24.0f, fontSize * 1.25f, fontSize * 3.0f);
    const float y0 = bodyTop + fontSize * 0.9f;  // where the top-most beat sits at the default view

    // Pan and zoom. The wheel scrolls through time; Ctrl+wheel zooms around the mouse; Home or
    // Ctrl+0 resets. firstBeat is the (fractional) beat drawn at y0.
    {
        ImGuiIO& io = ImGui::GetIO();
        if (canvasHovered && io.KeyCtrl && io.MouseWheel != 0.0f) {
            const float oldSpacing = baseSpacing * edit.zoom;
            const float mouseBeat = edit.firstBeat + (mouse.y - y0) / oldSpacing;
            edit.zoom = std::clamp(edit.zoom * std::pow(1.15f, io.MouseWheel), kMinLadderZoom,
                                   kMaxLadderZoom);
            edit.firstBeat = mouseBeat - (mouse.y - y0) / (baseSpacing * edit.zoom);
        } else if (canvasHovered && !io.KeyCtrl) {
            // Wheel up moves toward earlier beats. ~2 beats per notch, scaled so the same
            // distance on screen moves at any zoom.
            const float beatsPerNotch = 2.0f / edit.zoom;
            edit.firstBeat -= io.MouseWheel * beatsPerNotch;
        }
        // Only while the mouse is over the ladder: Home over the juggler resets the camera.
        if (ImGui::IsWindowHovered() && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Home) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0))))
            resetLadderView(edit);
    }
    const float beatSpacing = baseSpacing * edit.zoom;
    const float firstBeat = edit.firstBeat;
    auto beatY = [&](int b) { return y0 + (static_cast<float>(b) - firstBeat) * beatSpacing; };
    // Beats with any part on screen (a little margin either side for markers and labels).
    const int bFirst = static_cast<int>(std::floor(firstBeat - (y0 - bodyTop) / beatSpacing)) - 1;
    const int bLast = static_cast<int>(std::ceil(firstBeat + (maxPt.y - y0) / beatSpacing)) + 1;
    // Remember whether beat 1 was visible, for the toolbar's Reset View button.
    edit.beatOneVisible = beatY(0) >= bodyTop && beatY(0) <= maxPt.y;

    // Every juggler throws right on even beats (beat 0 is "beat 1"). Works for negative beats.
    // (Unless their hands are swapped, LRswap: then the left hand throws on beat 1.)
    auto rightHandBeat = [&](int j, int b) { return loop.rightHandBeat(j, b); };
    auto slotPoint = [&](Slot s) { return ImVec2(columnX(s.juggler, rightHandBeat(s.juggler, s.beat)), beatY(s.beat)); };

    // Curve for a throw made from `from`.
    auto throwCurve = [&](Slot from, const LoopThrow& t) {
        Curve c;
        const Slot to = landingSlot(from, t);
        c.p0 = slotPoint(from);
        c.p1 = slotPoint(to);
        const float dy = c.p1.y - c.p0.y;
        if (to.juggler == from.juggler && t.value % 2 == 0) {
            // Same hand: arch outside the juggler's columns (right hand to the right, left
            // hand to the left), wider for higher throws, reaching the edge of the room at 8.
            const float bulge = outsideRoomOf(from.juggler) * 0.9f * std::min(1.0f, static_cast<float>(t.value) / 8.0f);
            const float dir = rightHandBeat(from.juggler, from.beat) ? 1.0f : -1.0f;
            const float cx = c.p0.x + dir * bulge * (4.0f / 3.0f);  // cubic peak ~= bulge
            c.c1 = ImVec2(cx, c.p0.y + dy * 0.1f);
            c.c2 = ImVec2(cx, c.p1.y - dy * 0.1f);
        } else {
            // To the other hand, or to another juggler: a gentle S-curve.
            c.c1 = ImVec2(c.p0.x, c.p0.y + dy * 0.35f);
            c.c2 = ImVec2(c.p1.x, c.p1.y - dy * 0.35f);
        }
        return c;
    };
    // Rubber band from a fixed point to the mouse when it isn't over a valid target.
    auto looseCurve = [&](ImVec2 from, ImVec2 to) {
        Curve c;
        c.p0 = from;
        c.p1 = to;
        const float dy = to.y - from.y;
        c.c1 = ImVec2(from.x, from.y + dy * 0.35f);
        c.c2 = ImVec2(to.x, to.y - dy * 0.35f);
        return c;
    };

    const ImU32 railCol = IM_COL32(150, 155, 170, 255);
    const ImU32 rungRightCol = IM_COL32(110, 116, 132, 255);
    const ImU32 rungLeftCol = IM_COL32(58, 62, 74, 255);
    const ImU32 textCol = IM_COL32(150, 155, 170, 255);
    const ImU32 dimText = IM_COL32(100, 104, 116, 255);
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 ghostColor = IM_COL32(170, 172, 180, 255);  // a held empty beat (a 0)

    // The body (everything below the strip headers) is clipped, so throws scrolled up slide
    // under the headers.
    dl->PushClipRect(ImVec2(origin.x, bodyTop), maxPt, true);

    // Background: alternate shades every loop period, so the repeat stands out. Plain grays,
    // independent of color-vision mode, kept very dark so throw colors keep their contrast.
    if (period > 0) {
        const ImU32 periodShades[2] = {kCanvasBackground, IM_COL32(24, 26, 32, 255)};
        for (int b = bFirst; b <= bLast; ++b) {
            const float top = beatY(b) - beatSpacing * 0.5f;
            const int periodIndex = b >= 0 ? b / period : -((-b + period - 1) / period);
            dl->AddRectFilled(ImVec2(origin.x, top), ImVec2(maxPt.x, top + beatSpacing),
                              periodShades[positiveMod(periodIndex, 2)]);
        }
    }

    // Beat lines across each strip, so any point on a throw can be traced straight across to
    // its beat. Each strip styles its own lines by its juggler's hand on that beat: right-hand
    // beats get a brighter, heavier line, so a throw's hand can be read from the line it starts
    // on. Beat numbers in the gutter; when zoomed out, only every n-th beat is numbered so they
    // don't collide (always including beat 1).
    const int labelStep = std::max(1, static_cast<int>(std::ceil(fontSize * 1.1f / beatSpacing)));
    for (int b = bFirst; b <= bLast; ++b) {
        const float y = beatY(b);
        for (int j = 0; j < jugglers; ++j) {
            const bool right = rightHandBeat(j, b);
            dl->AddLine(ImVec2(stripLeft(j) + fontSize * 0.2f, y), ImVec2(stripLeft(j) + widthOf(j) - fontSize * 0.2f, y),
                        right ? rungRightCol : rungLeftCol, right ? 2.0f : 1.0f);
        }
        if (positiveMod(b, labelStep) != 0) continue;
        const std::string label = std::to_string(b + 1);  // beat 0 is "beat 1"
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(areaLeft - fontSize * 0.6f - ts.x, y - ts.y * 0.5f), dimText, label.c_str());
    }
    for (int j = 0; j < jugglers; ++j) {
        for (int right = 0; right < 2; ++right) {
            const float x = columnX(j, right != 0);
            dl->AddLine(ImVec2(x, bodyTop), ImVec2(x, maxPt.y), railCol, 2.0f);
        }
    }

    if (period == 0) {
        dl->PopClipRect();
        const char* msg = "No valid pattern to display. Enter a siteswap below.";
        dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f), dimText, msg);
        dl->PopClipRect();
        return result;
    }

    // The throw made from a slot. While a chain is open, its local edits are applied (and the
    // held throw's departure reads kNoThrow). In a sketch, open throws read kOpenThrow.
    auto throwAt = [&](Slot s) {
        return chain.active ? editThrowAt(chain, s) : loop.at(s.juggler, positiveMod(s.beat, period));
    };
    auto loopSlot = [&](Slot s) { return s.juggler * period + positiveMod(s.beat, period); };
    // Drawing stops when the spot it was drawing from has been decided some other way (undo,
    // typing, or the sketch being completed).
    if (edit.drawing && edit.drawEnd == HeldEnd::Arrival &&
        loop.at(edit.drawFrom.juggler, edit.drawFrom.beat).value != kOpenThrow)
        edit.drawing = false;
    if (edit.drawing && edit.drawEnd == HeldEnd::Departure && loopIncoming(loop)[static_cast<size_t>(loopSlot(edit.drawTo))] >= 0)
        edit.drawing = false;  // something lands in drawTo again (undo, typing)

    // ---- Prop identity (for colors) ----------------------------------------------------------
    // In a repeating pattern each prop follows a fixed cycle through the loop's throws, so a
    // throw's prop can be worked out from its slot alone. Ids therefore don't depend on what
    // part of the ladder is on screen, and scrolling never recolors anything. With "color by
    // orbit", every prop on the same orbit shares its orbit's style. A sketch has no definite
    // props yet: its throws are colored by path.
    const BallOrbits orbits = sketch ? BallOrbits() : computeBallOrbits(loop);
    const LoopOrbits loopOrbits = computeLoopOrbits(loop);
    const int totalBalls = orbits.totalBalls;
    const std::vector<int> orbitOfBall =
        (!sketch && options.colorByOrbit) ? orbitOfEachBall(loop, orbits, loopOrbits)
                                          : std::vector<int>(static_cast<size_t>(std::max(0, totalBalls)), 0);

    // While a chain is open, follow the balls through the edit: start well before anything the
    // chain touched (where the pattern is unedited, so orbit ids apply), and track landings
    // forward. The held throw is treated as if it had gone back to the empty spot, so it keeps
    // its own ball and nothing else shares its color.
    auto virtualAt = [&](Slot s) {
        if (chain.active) {
            if (chain.end == HeldEnd::Arrival && s == chain.fixed) {
                LoopThrow t;
                t.value = std::max(0, chain.hole.beat - chain.fixed.beat);
                t.dest = chain.hole.juggler;
                return t;
            }
            if (chain.end == HeldEnd::Departure && s == chain.hole) {
                LoopThrow t;
                t.value = std::max(0, chain.fixed.beat - chain.hole.beat);
                t.dest = chain.fixed.juggler;
                return t;
            }
        }
        return throwAt(s);
    };
    std::map<Slot, int> chainBallOf;  // departure slot -> ball id
    if (chain.active) {
        int lo = std::min(chain.fixed.beat, chain.hole.beat);
        if (!chain.overrides.empty()) lo = std::min(lo, chain.overrides.begin()->first.beat);
        const int simStart = std::min(bFirst - 40, lo - 40);
        std::map<Slot, int> arriving;  // landing slot -> ball id
        for (int d = simStart - 36; d < simStart; ++d) {  // balls already in the air
            for (int j = 0; j < jugglers; ++j) {
                const LoopThrow& t = loop.at(j, positiveMod(d, period));
                if (t.value > 0 && d + t.value >= simStart) arriving[landingSlot(Slot{j, d}, t)] = orbitBallAt(orbits, j, d);
            }
        }
        for (int b = simStart; b <= bLast; ++b) {
            for (int j = 0; j < jugglers; ++j) {
                const Slot s{j, b};
                const LoopThrow t = virtualAt(s);
                if (t.value <= 0) continue;
                std::map<Slot, int>::iterator it = arriving.find(s);
                const int ball = it != arriving.end() ? it->second : orbitBallAt(orbits, j, b);
                if (it != arriving.end()) arriving.erase(it);
                chainBallOf[s] = ball;
                arriving[landingSlot(s, t)] = ball;
            }
        }
    }
    auto ballAt = [&](Slot s) {
        if (!chain.active) return orbitBallAt(orbits, s.juggler, s.beat);
        std::map<Slot, int>::const_iterator it = chainBallOf.find(s);
        return it != chainBallOf.end() ? it->second : -1;
    };
    // The ball style for the throw made from a slot.
    auto styleAt = [&](Slot s) {
        if (sketch) return std::max(0, loopOrbits.idAt(s.juggler, s.beat));
        const int ball = std::max(0, ballAt(s));
        return options.colorByOrbit && ball < totalBalls ? orbitOfBall[static_cast<size_t>(ball)] : ball;
    };
    if (chain.active) {
        const Slot heldFrom = chain.end == HeldEnd::Arrival ? chain.fixed : chain.hole;
        const int ball = chain.held.value == 0 ? -1 : ballAt(heldFrom);
        edit.heldBall = ball < 0 ? -1 : (options.colorByOrbit && ball < totalBalls ? orbitOfBall[static_cast<size_t>(ball)] : ball);
    }

    // The throws drawn this frame, including throws made above the top edge that are still in
    // the air on screen.
    std::vector<DrawnThrow> drawn;
    for (int b = bFirst - 36; b <= bLast; ++b) {
        for (int j = 0; j < jugglers; ++j) {
            const Slot s{j, b};
            const LoopThrow t = throwAt(s);
            if (t.value <= 0 || b + t.value < bFirst) continue;
            drawn.push_back({s, t, styleAt(s), throwCurve(s, t)});
        }
    }
    // In a sketch: which spots nothing lands in yet.
    const std::vector<int> incoming = sketch ? loopIncoming(loop) : std::vector<int>();
    auto nothingLandsIn = [&](Slot s) { return sketch && incoming[static_cast<size_t>(loopSlot(s))] < 0; };

    // ---- Interaction -------------------------------------------------------------------------
    const float hitRadius = std::max(6.0f, fontSize * 0.5f);
    const float ambiguityMargin = 3.0f;  // the nearest curve must win by this much
    const float stickyRadius = hitRadius * 1.6f;  // keep the current hover while within this
    const float snapRadius = std::max(beatSpacing * 0.45f, fontSize * 0.9f);  // grabbing a slot's point

    // Nearest drawn throw to the mouse, with "clearly nearest" and hysteresis rules. `keep`
    // says whether a candidate is the one currently highlighted. `allowHalf(t)` filters by
    // which half of a curve the nearest point is on; curves failing it are ignored entirely
    // (they don't count as competitors either).
    auto pickThrow = [&](auto keep, auto allowHalf) {
        HitResult best, second, kept;
        for (int i = 0; i < static_cast<int>(drawn.size()); ++i) {
            const Curve& c = drawn[static_cast<size_t>(i)].curve;
            float t = 0.0f;
            const float d = bezierDistance(c.p0, c.c1, c.c2, c.p1, mouse, &t);
            if (!allowHalf(t)) continue;
            if (keep(drawn[static_cast<size_t>(i)], t) && d <= stickyRadius) kept = {i, t, d};
            if (d < best.distance) {
                second = best;
                best = {i, t, d};
            } else if (d < second.distance) {
                second = {i, t, d};
            }
        }
        if (kept.index >= 0 && kept.distance <= best.distance + ambiguityMargin) return kept;
        if (best.distance <= hitRadius && second.distance - best.distance >= ambiguityMargin)
            return best;
        return HitResult();
    };
    // The slot point nearest the mouse among those on screen.
    auto nearestSlot = [&](float* distance) {
        Slot nearest;
        float nearestD = 1e9f;
        for (int b = bFirst; b <= bLast; ++b) {
            for (int j = 0; j < jugglers; ++j) {
                const Slot s{j, b};
                const ImVec2 p = slotPoint(s);
                const float dd = std::hypot(p.x - mouse.x, p.y - mouse.y);
                if (dd < nearestD) {
                    nearestD = dd;
                    nearest = s;
                }
            }
        }
        *distance = nearestD;
        return nearest;
    };

    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput;
    if (chain.active && ((canvasHovered && rightClicked) || (canvasHovered && escapePressed))) cancelLadderEdit(edit);
    bool justStopped = false;  // the right-click that stopped drawing doesn't also open a menu
    if (edit.drawing && ((canvasHovered && rightClicked) || escapePressed)) {
        edit.drawing = false;
        justStopped = true;
    }

    // Strip headers: with several jugglers, clicking one's number selects that juggler
    // (clicking the selected one deselects). A separate small button to its left, with the
    // triangle, collapses or expands the strip.
    const float toggleWidth = fontSize * 1.3f;
    const float toggleGap = fontSize * 0.35f;
    auto jugglerHeaderRect = [&](int j, ImVec2* min, ImVec2* max) {
        const float labelWidth = ImGui::CalcTextSize("J00").x + fontSize * 0.9f;
        const float groupLeft = stripLeft(j) + widthOf(j) * 0.5f - 0.5f * (toggleWidth + toggleGap + labelWidth);
        const float top = origin.y + headerHeight + fontSize * 0.15f;
        *min = ImVec2(groupLeft + toggleWidth + toggleGap, top);
        *max = ImVec2(min->x + labelWidth, top + fontSize * 1.3f);
    };
    auto collapseToggleRect = [&](int j, ImVec2* min, ImVec2* max) {
        ImVec2 a, b;
        jugglerHeaderRect(j, &a, &b);
        *min = ImVec2(a.x - toggleGap - toggleWidth, a.y);
        *max = ImVec2(a.x - toggleGap, b.y);
    };
    int hoverHeader = -1;
    bool hoverToggle = false;  // over the strip's collapse button rather than its number
    if (jugglers > 1 && canvasHovered && !chain.active && !edit.drawing) {
        for (int j = 0; j < jugglers; ++j) {
            ImVec2 a, b, ta, tb;
            jugglerHeaderRect(j, &a, &b);
            collapseToggleRect(j, &ta, &tb);
            if (mouse.y < a.y || mouse.y > b.y) continue;
            if (mouse.x >= a.x && mouse.x <= b.x) {
                hoverHeader = j;
                hoverToggle = false;
            } else if (mouse.x >= ta.x && mouse.x <= tb.x) {
                hoverHeader = j;
                hoverToggle = true;
            }
        }
        if (hoverHeader >= 0 && leftClicked) {
            if (hoverToggle && hoverHeader < kMaxJugglers)
                edit.collapsed[static_cast<size_t>(hoverHeader)] = !edit.collapsed[static_cast<size_t>(hoverHeader)];
            else
                result.selectJuggler = hoverHeader == options.selectedJuggler ? -1 : hoverHeader;
        }
    }
    const bool inBody = mouse.y >= bodyTop;

    // Scrubbing: click or drag in the beat-number column to move the playhead there.
    const bool overGutter = canvasHovered && inBody && mouse.x < areaLeft && !chain.active && !edit.drawing;
    if (overGutter && leftClicked) edit.scrubbing = true;
    if (edit.scrubbing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            result.scrubbing = true;
            result.scrubBeat = static_cast<double>(firstBeat) + static_cast<double>((mouse.y - y0) / beatSpacing);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        } else {
            edit.scrubbing = false;
            result.scrubEnded = true;
        }
    } else if (overGutter) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) ImGui::SetTooltip("Click or drag here to move the playhead");
    }

    // Idle hover: which end of which throw would a click pick up?
    int hoverIndex = -1;
    HeldEnd hoverEnd = HeldEnd::Arrival;
    bool hoverZero = false;  // an empty beat (0) under the mouse...
    Slot hoverZeroSlot;      // ...here
    bool hoverOpen = false;  // a sketch's open throw ("?") under the mouse...
    Slot hoverOpenSlot;      // ...here
    bool hoverInbound = false;  // a sketch spot that throws but that nothing lands in, under the mouse...
    Slot hoverInboundSlot;      // ...here
    // Holding (or drawing): which slot would a click drop the held end on?
    bool haveTarget = false;
    Slot target;

    if (canvasHovered && inBody && !chain.active && !edit.drawing) {
        // Empty beats (0s) can be picked up too (they "land" on their own beat). In a sketch,
        // open throws ("?") are where drawing starts, and a spot that throws something but that
        // nothing lands in is where a catch can be drawn into. Right on one of those points, it
        // wins over the curves passing by.
        {
            float bestD = fontSize * 0.85f;  // (a "?" ring's radius is 0.8 em)
            for (int b = bFirst; b <= bLast; ++b) {
                for (int j = 0; j < jugglers; ++j) {
                    const Slot s{j, b};
                    const int v = throwAt(s).value;
                    const bool inbound = sketch && v > 0 && nothingLandsIn(s);
                    if (v != 0 && !(sketch && v == kOpenThrow) && !inbound) continue;
                    const ImVec2 p = slotPoint(s);
                    const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                    if (d < bestD) {
                        bestD = d;
                        hoverZero = v == 0;
                        hoverOpen = v == kOpenThrow;
                        hoverInbound = inbound;
                        hoverZeroSlot = hoverOpenSlot = hoverInboundSlot = s;
                    }
                }
            }
        }
        // The half of a throw nearest the mouse is the end a click picks up: its catch, or its
        // throw (where it leaves from).
        const HitResult hit = (hoverZero || hoverOpen || hoverInbound) ? HitResult() : pickThrow([&](const DrawnThrow& d, float t) {
            const HeldEnd e = t >= 0.5f ? HeldEnd::Arrival : HeldEnd::Departure;
            return edit.hovering && d.from == edit.hoverSlot && e == edit.hoverEnd;
        }, [](float) { return true; });
        if (hit.index >= 0) {
            const DrawnThrow& d = drawn[static_cast<size_t>(hit.index)];
            hoverIndex = hit.index;
            // Small dead zone around the midpoint so the chosen half doesn't flicker.
            if (edit.hovering && d.from == edit.hoverSlot && std::fabs(hit.t - 0.5f) < 0.06f)
                hoverEnd = edit.hoverEnd;
            else
                hoverEnd = hit.t < 0.5f ? HeldEnd::Departure : HeldEnd::Arrival;
        }
        edit.hovering = hoverIndex >= 0;
        if (edit.hovering) edit.hoverSlot = drawn[static_cast<size_t>(hoverIndex)].from;
        edit.hoverEnd = hoverEnd;

        if (leftClicked && sketch && (hoverIndex >= 0 || hoverZero)) {
            // In a sketch, picking up a throw takes it out (its spot becomes "?"), to be drawn
            // again: by its catch, from where it was thrown to wherever it should land; by its
            // throw, from wherever it should leave to where it lands.
            const Slot from = hoverIndex >= 0 ? drawn[static_cast<size_t>(hoverIndex)].from : hoverZeroSlot;
            const LoopThrow t = throwAt(from);
            result.committed = true;
            result.loop = deleteThrow(loop, loopSlot(from));
            result.startedDrawing = true;
            edit.drawing = true;
            edit.drawEnd = (hoverIndex >= 0 && hoverEnd == HeldEnd::Departure) ? HeldEnd::Departure : HeldEnd::Arrival;
            edit.drawFrom = from;
            edit.drawTo = landingSlot(from, t);
        } else if (leftClicked && hoverInbound) {
            // Draw a catch into this spot: pick the spot the prop is thrown from.
            result.startedDrawing = true;
            edit.drawing = true;
            edit.drawEnd = HeldEnd::Departure;
            edit.drawTo = hoverInboundSlot;
        } else if (leftClicked && hoverOpen) {
            result.startedDrawing = true;
            edit.drawing = true;
            edit.drawEnd = HeldEnd::Arrival;
            edit.drawFrom = hoverOpenSlot;
        } else if (leftClicked && hoverIndex >= 0) {
            chain = beginEditChain(loop, drawn[static_cast<size_t>(hoverIndex)].from, hoverEnd);
            result.startedChain = true;
        } else if (leftClicked && hoverZero) {
            chain = beginEditChain(loop, hoverZeroSlot, HeldEnd::Arrival);
            result.startedChain = true;
        }
    } else if (canvasHovered && inBody && edit.drawing && edit.drawEnd == HeldEnd::Departure) {
        // Holding a throw by its start: the nearest spot within snapping distance is where it
        // would be thrown from (it still lands in drawTo).
        float nearestD = 0.0f;
        const Slot nearest = nearestSlot(&nearestD);
        if (nearestD <= snapRadius && drawnThrowFor(nearest, edit.drawTo, nullptr)) {
            haveTarget = true;
            target = nearest;
        }
        if (haveTarget && !leftClicked) {
            bool displacedAny = false;
            Slot displacedLanding;
            result.hasPreview = rethrowFrom(loop, target, edit.drawTo, &result.preview, &displacedAny, &displacedLanding);
        }
        if (leftClicked && haveTarget && !result.startedDrawing) {
            JugglingLoop changed;
            bool displacedAny = false;
            Slot displacedLanding;
            if (rethrowFrom(loop, target, edit.drawTo, &changed, &displacedAny, &displacedLanding)) {
                result.committed = true;
                result.loop = changed;
                // A throw that was already leaving from there now needs a new spot to leave from.
                if (displacedAny) edit.drawTo = displacedLanding;
                else edit.drawing = false;
                haveTarget = false;
            }
        }
    } else if (canvasHovered && inBody && edit.drawing) {
        // Drawing: the nearest spot within snapping distance is where the prop would land.
        float nearestD = 0.0f;
        const Slot nearest = nearestSlot(&nearestD);
        if (nearestD <= snapRadius && drawnThrowFor(edit.drawFrom, nearest, nullptr)) {
            haveTarget = true;
            target = nearest;
        }
        if (haveTarget && !leftClicked) {
            // Show the jugglers what this would be.
            bool displacedAny = false;
            Slot displaced;
            result.hasPreview = drawThrow(loop, edit.drawFrom, target, &result.preview, &displacedAny, &displaced);
        }
        if (leftClicked && haveTarget && !result.startedDrawing) {
            JugglingLoop drawnLoop;
            bool displacedAny = false;
            Slot displaced;
            if (drawThrow(loop, edit.drawFrom, target, &drawnLoop, &displacedAny, &displaced)) {
                result.committed = true;
                result.loop = drawnLoop;
                // Carry on: with a prop that lost its spot, it needs a new home first; otherwise
                // the prop just caught is thrown next, unless that throw is already drawn (the
                // path joins one that exists, or closes on itself).
                if (displacedAny) {
                    edit.drawFrom = displaced;
                } else if (drawnLoop.at(target.juggler, positiveMod(target.beat, period)).value == kOpenThrow) {
                    edit.drawFrom = target;
                } else {
                    edit.drawing = false;
                }
                haveTarget = false;
            }
        }
    } else if (canvasHovered && inBody && chain.active) {
        // Near a slot's point, that slot is the target: several curves meet there, and the
        // slot under the mouse is what the user means. Elsewhere, a curve under the mouse
        // targets its relevant end (its landing for a held arrival, its start for a held
        // departure), and failing that the nearest slot within snapping distance.
        const bool arrivalMode = chain.end == HeldEnd::Arrival;
        const float pointPriorityRadius = fontSize * 0.9f;
        float nearestD = 0.0f;
        const Slot nearest = nearestSlot(&nearestD);
        if (nearestD <= pointPriorityRadius) {
            haveTarget = true;
            target = nearest;
        } else {
            // Only the matching half of other throws counts: a held catch is dropped by pointing
            // at another throw's incoming (catch) half, a held throw end at an outgoing half.
            // Otherwise a throw leaving the slot you're aiming at would steal the drop.
            const HitResult hit = pickThrow(
                [&](const DrawnThrow& d, float) {
                    return edit.targeting && (arrivalMode ? d.to() : d.from) == edit.hoverTarget;
                },
                [&](float t) { return arrivalMode ? t >= 0.5f : t <= 0.5f; });
            if (hit.index >= 0) {
                const DrawnThrow& d = drawn[static_cast<size_t>(hit.index)];
                haveTarget = true;
                target = arrivalMode ? d.to() : d.from;
            } else if (nearestD <= snapRadius) {
                haveTarget = true;
                target = nearest;
            }
        }
        if (haveTarget && !editDropThrow(chain, target, nullptr))
            haveTarget = false;  // not a legal drop: the rubber band just follows the mouse
        edit.targeting = haveTarget;
        edit.hoverTarget = target;

        if (leftClicked && haveTarget) {
            JugglingLoop newLoop;
            const DropResult r = editDrop(chain, target, shiftHeld, &newLoop);
            if (r == DropResult::Closed) {
                result.committed = true;
                result.loop = newLoop;
                cancelLadderEdit(edit);
            }
        }
    } else if (!canvasHovered || !inBody) {
        edit.hovering = false;
        edit.targeting = false;
    }

    // Right-click (when not editing): a menu for the throw under the mouse, or else for the
    // beat line nearest the mouse; on a juggler's number, that juggler's menu.
    const char* kContextMenu = "##ladder_context";
    if (canvasHovered && rightClicked && hoverHeader >= 0 && !hoverToggle && !chain.active && !edit.drawing) {
        edit.context = LadderEditState::Context::Juggler;
        edit.contextJuggler = hoverHeader;
        ImGui::OpenPopup(kContextMenu);
    }
    if (canvasHovered && inBody && rightClicked && !chain.active && !edit.drawing && !justStopped &&
        !result.startedChain && !result.startedDrawing) {
        // The nearest curve within reach (no "clearly nearest" rule here: the menu names the
        // throw it's for).
        int nearestThrow = -1;
        float nearestThrowD = hitRadius;
        for (int i = 0; i < static_cast<int>(drawn.size()); ++i) {
            const Curve& c = drawn[static_cast<size_t>(i)].curve;
            float t = 0.0f;
            const float d = bezierDistance(c.p0, c.c1, c.c2, c.p1, mouse, &t);
            if (d <= nearestThrowD) {
                nearestThrowD = d;
                nearestThrow = i;
            }
        }
        if (nearestThrow >= 0) {
            edit.context = LadderEditState::Context::Throw;
            edit.contextSlot = drawn[static_cast<size_t>(nearestThrow)].from;
        } else {
            edit.context = LadderEditState::Context::Beat;
            edit.contextBeat = static_cast<int>(std::lround(firstBeat + (mouse.y - y0) / beatSpacing));
        }
        ImGui::OpenPopup(kContextMenu);
    }

    // ---- Drawing -----------------------------------------------------------------------------
    const float thickness = 2.5f;
    const float arrowSize = fontSize * 0.55f;
    const float markerRadius = fontSize * 0.36f;
    // Things you can click to start or finish something stand out more than ordinary markers:
    // a sketch's open throws ("?") and the spots a held or drawn throw can close on.
    const float openRadius = fontSize * 0.8f;     // ring radius
    const float targetRadius = fontSize * 1.1f;  // outside a "?" ring, which may sit inside it
    const ImU32 brightText = IM_COL32(225, 229, 238, 255);
    const float dashUnit = fontSize * 0.35f;

    // Throws: curves and arrowheads.
    for (const DrawnThrow& d : drawn) {
        const BallStyle style = ballStyle(colorVision, d.ball);
        const ImU32 color = muted(d.from.juggler, style.color);
        const Curve& c = d.curve;
        drawStyledBezier(dl, c.p0, c.c1, c.c2, c.p1, color, thickness, style.dash, dashUnit);
        drawBezierArrowHead(dl, c.p0, c.c1, c.c2, c.p1, color, arrowSize, markerRadius + 3.0f);
    }

    // A path picked out (hovering "Delete path" in the menu): every throw on it glows.
    if (!edit.pathHighlight.empty()) {
        for (const DrawnThrow& d : drawn) {
            const int hp = std::max(1, edit.pathHighlightPeriod);
            if (std::find(edit.pathHighlight.begin(), edit.pathHighlight.end(),
                          d.from.juggler * hp + positiveMod(d.from.beat, hp)) == edit.pathHighlight.end())
                continue;
            const Curve& c = d.curve;
            const float pulse = 0.6f + 0.4f * std::sin(time * 5.0f);
            dl->AddBezierCubic(c.p0, c.c1, c.c2, c.p1, mixColor(ballStyle(colorVision, d.ball).color, white, 0.5f, 0.45f * pulse),
                               thickness + 6.0f);
        }
    }

    // A throw hovered in the siteswap text box: it glows at every repeat (a 0 or a "?", which
    // has no curve, gets a ring on its spot).
    if (options.highlightThrow >= 0 && period > 0 && options.highlightThrow < loop.jugglers * period) {
        const float pulse = 0.6f + 0.4f * std::sin(time * 5.0f);
        bool curved = false;
        for (const DrawnThrow& d : drawn) {
            if (loopSlot(d.from) != options.highlightThrow) continue;
            const Curve& c = d.curve;
            dl->AddBezierCubic(c.p0, c.c1, c.c2, c.p1,
                               mixColor(ballStyle(colorVision, d.ball).color, white, 0.5f, 0.45f * pulse), thickness + 6.0f);
            curved = true;
        }
        if (!curved) {
            const int juggler = options.highlightThrow / period;
            const int beat = options.highlightThrow % period;
            for (int b = bFirst; b <= bLast; ++b) {
                if (positiveMod(b, period) != beat) continue;
                dl->AddCircle(slotPoint(Slot{juggler, b}), 10.0f, mixColor(ghostColor, white, 0.5f, 0.6f + 0.4f * pulse), 0, 3.0f);
            }
        }
    }

    // Idle hover highlight: just the throw under the mouse.
    if (hoverIndex >= 0) {
        const DrawnThrow& h = drawn[static_cast<size_t>(hoverIndex)];
        const Curve& c = h.curve;
        drawBezierHalfHighlight(dl, c.p0, c.c1, c.c2, c.p1, hoverEnd == HeldEnd::Departure,
                                ballStyle(colorVision, h.ball).color, 1.0f);
    }
    if (hoverZero || hoverOpen || hoverInbound)
        dl->AddCircle(slotPoint(hoverZeroSlot), 9.0f, mixColor(ghostColor, white, 0.5f, 0.9f), 0, 2.0f);

    // Holding: the empty spot(s), the rubber band and its repeats.
    if (chain.active) {
        const bool arrivalMode = chain.end == HeldEnd::Arrival;
        const float pulse = 0.5f + 0.5f * std::sin(time * 5.0f);

        // The empty spot that closes the chain: pulsing circles. Its copies in other repeats
        // that the chain could close on instead (Shift-click, changing the prop count) get
        // pulsing squares, dim normally and bright while Shift is held.
        for (int b = bFirst; b <= bLast; ++b) {
            if (positiveMod(b - chain.hole.beat, period) != 0) continue;
            const Slot s{chain.hole.juggler, b};
            const ImVec2 p = slotPoint(s);
            if (s == chain.hole) {
                dl->AddCircle(p, targetRadius + 2.0f + 3.0f * pulse, mixColor(white, white, 0.0f, 0.95f), 0, 2.5f);
                dl->AddCircle(p, targetRadius - 3.0f, mixColor(white, white, 0.0f, 0.6f), 0, 1.5f);
            } else if (editCloseLoopLength(chain, s, true) > 0) {
                const float a = shiftHeld ? 0.95f : 0.35f;
                const float outer = targetRadius + 1.0f + 3.0f * pulse;
                const float inner = targetRadius - 3.0f;
                dl->AddRect(ImVec2(p.x - outer, p.y - outer), ImVec2(p.x + outer, p.y + outer),
                            mixColor(white, white, 0.0f, a), 0.0f, 0, 2.0f);
                dl->AddRect(ImVec2(p.x - inner, p.y - inner), ImVec2(p.x + inner, p.y + inner),
                            mixColor(white, white, 0.0f, a * 0.6f), 0.0f, 0, 1.5f);
            }
        }

        const bool ghost = edit.heldBall < 0;
        const BallStyle heldStyle = ghost ? BallStyle{ghostColor, MarkerShape::Circle, DashPattern::ShortDash}
                                          : ballStyle(colorVision, edit.heldBall);

        // Repeats of the edit are spaced by the loop length it would get if closed now.
        const int echoSpacing = editProvisionalLoopLength(chain);

        // The held throw's curve for one repeat (m = 0 is the one being dragged).
        auto heldCurve = [&](int m) {
            const int shift = m * echoSpacing;
            if (haveTarget) {
                Slot shiftedTarget = target;
                shiftedTarget.beat += shift;
                Slot shiftedFixed = chain.fixed;
                shiftedFixed.beat += shift;
                LoopThrow t;
                Slot from;
                // Same throw as the real drop, just shifted (editDropThrow already said it's legal).
                if (arrivalMode) {
                    from = shiftedFixed;
                    t.value = shiftedTarget.beat - shiftedFixed.beat;
                    t.dest = shiftedTarget.juggler;
                } else {
                    from = shiftedTarget;
                    t.value = shiftedFixed.beat - shiftedTarget.beat;
                    t.dest = shiftedFixed.juggler;
                }
                return throwCurve(from, t);
            }
            Slot fixedShifted = chain.fixed;
            fixedShifted.beat += shift;
            const ImVec2 fixedP = slotPoint(fixedShifted);
            const ImVec2 loose(mouse.x, mouse.y + static_cast<float>(shift) * beatSpacing);
            return arrivalMode ? looseCurve(fixedP, loose) : looseCurve(loose, fixedP);
        };

        // Dull impressions of the edit in the nearest few repeats, fading with distance. Only
        // while the mouse is over a valid drop target, so they show where the edit would go
        // rather than chasing the mouse. Skipped for a 1-beat spacing: then every throw is a
        // copy, which says nothing useful.
        if (haveTarget && echoSpacing >= 2) {
            for (int m = -3; m <= 3; ++m) {
                if (m == 0) continue;
                const Curve c = heldCurve(m);
                if (std::max(c.p0.y, c.p1.y) < bodyTop || std::min(c.p0.y, c.p1.y) > maxPt.y) continue;
                const float fade = 0.8f - 0.2f * static_cast<float>(std::abs(m));
                drawStyledBezier(dl, c.p0, c.c1, c.c2, c.p1,
                                 mixColor(heldStyle.color, kCanvasBackground, 0.45f, fade), 2.0f,
                                 heldStyle.dash, dashUnit);
            }
        }

        // The held throw itself.
        const Curve c = heldCurve(0);
        drawHeldThrowEffects(dl, c.p0, c.c1, c.c2, c.p1, heldStyle.color, time, 1.0f);
        drawStyledBezier(dl, c.p0, c.c1, c.c2, c.p1, heldStyle.color, thickness + 0.5f, heldStyle.dash, dashUnit);
        if (haveTarget) {
            drawBezierArrowHead(dl, c.p0, c.c1, c.c2, c.p1, heldStyle.color, arrowSize, markerRadius + 3.0f);
            // Highlight the target the way a hovered end is highlighted.
            drawBezierHalfHighlight(dl, c.p0, c.c1, c.c2, c.p1, !arrivalMode, heldStyle.color, 1.0f);
        } else {
            dl->AddCircleFilled(arrivalMode ? c.p1 : c.p0, 4.0f, mixColor(heldStyle.color, white, 0.5f, 0.9f));
        }
    }

    // Drawing in a sketch: faint rings on the spots nothing lands in yet (where the prop can be
    // caught without displacing anything), and the rubber band from the open throw.
    if (edit.drawing) {
        const bool byStart = edit.drawEnd == HeldEnd::Departure;
        // The path being drawn takes the color of whatever lands in its starting spot (or, when
        // holding a throw by its start, of the path it lands on); a new path gets a new color.
        int pathStyle = loopOrbits.count;
        if (byStart) {
            const int next = loopOrbits.idAt(edit.drawTo.juggler, edit.drawTo.beat);
            if (next >= 0) pathStyle = next;
        } else {
            const int into = incoming[static_cast<size_t>(loopSlot(edit.drawFrom))];
            if (into >= 0) pathStyle = std::max(0, loopOrbits.idAt(into / period, into % period));
        }
        const BallStyle style = ballStyle(colorVision, pathStyle);
        // Faint rings on the spots it can go without displacing anything: spots nothing lands
        // in yet (holding a catch), or undecided throws (holding a throw by its start).
        for (int b = bFirst; b <= bLast; ++b) {
            for (int j = 0; j < jugglers; ++j) {
                const Slot s{j, b};
                const bool free = byStart ? (throwAt(s).value == kOpenThrow && drawnThrowFor(s, edit.drawTo, nullptr))
                                          : (nothingLandsIn(s) && drawnThrowFor(edit.drawFrom, s, nullptr));
                if (!free) continue;
                dl->AddCircle(slotPoint(s), targetRadius, mixColor(style.color, white, 0.25f, 0.9f), 0, 2.5f);
            }
        }
        Curve c;
        if (haveTarget) {
            LoopThrow t;
            const Slot from = byStart ? target : edit.drawFrom;
            drawnThrowFor(from, byStart ? edit.drawTo : target, &t);
            c = throwCurve(from, t);
        } else {
            c = byStart ? looseCurve(mouse, slotPoint(edit.drawTo)) : looseCurve(slotPoint(edit.drawFrom), mouse);
        }
        drawHeldThrowEffects(dl, c.p0, c.c1, c.c2, c.p1, style.color, time, 1.0f);
        drawStyledBezier(dl, c.p0, c.c1, c.c2, c.p1, style.color, thickness + 0.5f, style.dash, dashUnit);
        if (haveTarget || byStart) drawBezierArrowHead(dl, c.p0, c.c1, c.c2, c.p1, style.color, arrowSize, markerRadius + 3.0f);
        if (!haveTarget) dl->AddCircleFilled(byStart ? c.p0 : c.p1, 4.0f, mixColor(style.color, white, 0.5f, 0.9f));
    }

    // Playhead: where the jugglers are now, at sub-beat resolution, plus the same moment in
    // every other repeat on screen (fainter). Positioned with floats so it glides.
    //
    // The repeat is how long until the ladder's colors repeat: the same prop in the same hand.
    // Hands alternate, so an odd period takes two loops to come round; and with a color per prop,
    // the props on an orbit take turns, so it takes as many loops as that orbit has props (all
    // orbits at once: the least common multiple). With colors by orbit (or by path, in a
    // sketch), only the hands matter. Spacing the copies like that means the bright one always
    // shows the prop the jugglers are really throwing (in "3", it sweeps 6 beats, not 1).
    //
    // The bright copy is the one on beats 1 to `cycle`: going from beat `cycle` to the next it
    // fades to dim over that beat while the copy coming down from beat 0 to beat 1 brightens,
    // so there's always exactly one bright playhead and it never jumps.
    {
        auto gcd = [](long long a, long long b) {
            while (b != 0) {
                const long long r = a % b;
                a = b;
                b = r;
            }
            return a;
        };
        auto lcm = [&](long long a, long long b) { return a / gcd(a, b) * b; };
        long long cycle = period % 2 == 0 ? period : 2LL * period;  // hands come round
        if (!sketch && !options.colorByOrbit) {
            long long lapsForProps = 1;  // loops until every orbit's props are back in place
            for (const int n : orbits.balls)
                if (n > 1) lapsForProps = std::min(lcm(lapsForProps, n), 1000000LL);
            cycle = lcm(cycle, static_cast<long long>(period) * lapsForProps);
        }
        const double repeat = static_cast<double>(std::min(cycle, 1000000LL));
        const float left = areaLeft - fontSize * 0.3f;
        const float right = stripsRight;
        const double localBeat = static_cast<double>(firstBeat);
        // Copies at playheadBeat + k*repeat; draw every one that lands on screen.
        const double firstCopy = playheadBeat - std::floor((playheadBeat - localBeat) / repeat) * repeat - repeat;
        for (double b = firstCopy; ; b += repeat) {
            const float y = y0 + static_cast<float>(b - localBeat) * beatSpacing;
            if (y > maxPt.y + 2.0f) break;
            if (y < bodyTop) continue;
            // Brightness: 1 on beats 1..cycle (b from 0 to cycle - 1), fading in from beat 0
            // and out past beat `cycle`, each over one beat.
            const float w = static_cast<float>(std::clamp(std::min(b + 1.0, repeat - b), 0.0, 1.0));
            const ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(90.0f + 130.0f * w));
            if (w > 0.0f) dl->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(255, 255, 255, static_cast<int>(40.0f * w)), 6.0f);
            dl->AddLine(ImVec2(left, y), ImVec2(right, y), col, 1.0f + w);
            // A small right-pointing cap, so playhead copies can't be mistaken for beat lines.
            const float cap = fontSize * (0.35f + 0.1f * w);
            dl->AddTriangleFilled(ImVec2(left - cap * 1.1f, y - cap), ImVec2(left - cap * 1.1f, y + cap),
                                  ImVec2(left + cap * 0.2f, y), col);
        }
    }

    // Departure markers on top, ringed in the background color so they stay readable where
    // curves cross them. Empty beats get a small hollow circle. In a sketch, an open throw is a
    // "?" in a large dashed ring.
    for (int b = bFirst; b <= bLast; ++b) {
        for (int j = 0; j < jugglers; ++j) {
            const Slot s{j, b};
            const LoopThrow t = throwAt(s);
            const ImVec2 p0 = slotPoint(s);
            if (t.value == kOpenThrow && sketch && !(edit.drawing && edit.drawEnd == HeldEnd::Arrival && s == edit.drawFrom)) {
                const float r = openRadius;
                dl->AddCircleFilled(p0, r + 1.0f, IM_COL32(38, 42, 54, 255));
                for (int k = 0; k < 10; ++k) {  // dashed ring
                    const float a0 = static_cast<float>(k) * 0.628319f, a1 = a0 + 0.38f;
                    dl->PathArcTo(p0, r, a0, a1, 4);
                    dl->PathStroke(brightText, 0, 2.0f);
                }
                // A larger "?", drawn twice half a pixel apart so it reads bold.
                const float qSize = fontSize * 1.5f;
                const ImVec2 qs = ImGui::GetFont()->CalcTextSizeA(qSize, FLT_MAX, 0.0f, "?");
                const ImVec2 qp(p0.x - qs.x * 0.5f, p0.y - qs.y * 0.5f);
                dl->AddText(ImGui::GetFont(), qSize, qp, brightText, "?");
                dl->AddText(ImGui::GetFont(), qSize, ImVec2(qp.x + 0.6f, qp.y), brightText, "?");
            } else if (t.value == 0) {
                dl->AddCircle(p0, fontSize * 0.25f, dimText, 0, 1.5f);
            } else if (t.value > 0) {
                const BallStyle style = ballStyle(colorVision, styleAt(s));
                drawMarker(dl, p0, markerRadius, style.shape, muted(j, style.color), 2.0f, kCanvasBackground);
            }
        }
    }
    if (chain.active) {
        // The held throw's departure marker (its departure reads kNoThrow while held).
        const bool ghost = edit.heldBall < 0;
        const BallStyle heldStyle = ghost ? BallStyle{ghostColor, MarkerShape::Circle, DashPattern::ShortDash}
                                          : ballStyle(colorVision, edit.heldBall);
        if (chain.end == HeldEnd::Arrival || haveTarget) {
            const Slot from = chain.end == HeldEnd::Arrival ? chain.fixed : target;
            drawMarker(dl, slotPoint(from), markerRadius + 1.0f, heldStyle.shape, heldStyle.color, 2.0f,
                       kCanvasBackground);
        }
    }

    // Throw values: a small label on each throw, a little way along it from where it's thrown
    // (so it reads as belonging to that throw, not to one of the others meeting at the marker).
    if (options.showValues) {
        auto drawValueLabel = [&](const Curve& c, const std::string& label, ImU32 color) {
            const float along = std::min(0.3f, (fontSize * 1.3f) / std::max(1.0f, std::hypot(c.p1.x - c.p0.x, c.p1.y - c.p0.y)));
            const ImVec2 p = bezierPoint(c.p0, c.c1, c.c2, c.p1, std::max(0.12f, along));
            const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            const ImVec2 pad(fontSize * 0.2f, fontSize * 0.05f);
            const ImVec2 a(p.x - ts.x * 0.5f - pad.x, p.y - ts.y * 0.5f - pad.y);
            const ImVec2 b(p.x + ts.x * 0.5f + pad.x, p.y + ts.y * 0.5f + pad.y);
            dl->AddRectFilled(a, b, kCanvasBackground, fontSize * 0.25f);
            dl->AddRect(a, b, color, fontSize * 0.25f, 0, 1.0f);
            dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y), IM_COL32(230, 232, 238, 255), label.c_str());
        };
        for (const DrawnThrow& d : drawn) {
            if (d.from.beat < bFirst - 1) continue;  // its label is above the top edge anyway
            if (isCollapsed(d.from.juggler)) continue;  // no room in a collapsed strip
            drawValueLabel(d.curve, throwLabel(d.t, d.from.juggler, jugglers, options.relativeTargets), ballStyle(colorVision, d.ball).color);
        }
    }

    // Tooltip describing what a click would do.
    auto describeThrow = [&](const LoopThrow& t, int thrower) {
        std::string what = "Becomes a " + throwLabel(t, thrower, jugglers, options.relativeTargets);
        if (t.dest != thrower) what += " (a pass to J" + std::to_string(t.dest + 1) + ")";
        return what + ".";
    };
    if (chain.active && haveTarget) {
        LoopThrow t;
        Slot from;
        editDropThrow(chain, target, &t, &from);
        const std::string what = describeThrow(t, from.juggler);
        const int closeLength = editCloseLoopLength(chain, target, shiftHeld);
        const int wouldBeLength = editCloseLoopLengthUnlimited(chain, target, shiftHeld);
        const int copyLength = (!shiftHeld && target != chain.hole) ? editCloseLoopLength(chain, target, true) : 0;
        if (closeLength == 0 && wouldBeLength > 0) {
            ImGui::SetTooltip("Can't close here: the loop would be %d beats long (the limit is %d).\n"
                              "Keep going and close somewhere that keeps the edit shorter,\n"
                              "or press Esc to cancel.",
                              wouldBeLength, kMaxLoopBeats);
        } else if (closeLength > 0) {
            const int delta = editDropBallDelta(chain, target);
            if (delta == 0)
                ImGui::SetTooltip("%s Fills the empty spot: done.\nThe edit repeats every %d beat%s.",
                                  what.c_str(), closeLength, closeLength == 1 ? "" : "s");
            else
                ImGui::SetTooltip("%s Closes on a copy of the empty spot: %s%d prop%s.\n"
                                  "The edit repeats every %d beat%s.",
                                  what.c_str(), delta > 0 ? "+" : "", delta, (delta == 1 || delta == -1) ? "" : "s",
                                  closeLength, closeLength == 1 ? "" : "s");
        } else if (copyLength > 0) {
            const int delta = editDropBallDelta(chain, target);
            ImGui::SetTooltip("%s Picks up the throw that was here.\n"
                              "(Shift-click to close here instead: %s%d prop%s.)",
                              what.c_str(), delta > 0 ? "+" : "", delta, (delta == 1 || delta == -1) ? "" : "s");
        } else {
            ImGui::SetTooltip("%s Picks up the throw that was here.", what.c_str());
        }
    } else if (edit.drawing && haveTarget && edit.drawEnd == HeldEnd::Departure) {
        LoopThrow t;
        drawnThrowFor(target, edit.drawTo, &t);
        const std::string what = describeThrow(t, target.juggler);
        if (throwAt(target).value == kOpenThrow || throwAt(target).value == kNoThrow)
            ImGui::SetTooltip("%s Thrown from here: done.", what.c_str());
        else
            ImGui::SetTooltip("%s Something is already thrown from here: you'll pick it up\n"
                              "and find it a new spot to be thrown from next.", what.c_str());
    } else if (edit.drawing && haveTarget) {
        LoopThrow t;
        drawnThrowFor(edit.drawFrom, target, &t);
        const std::string what = describeThrow(t, edit.drawFrom.juggler);
        const int targetSlot = loopSlot(target);
        const bool taken = incoming[static_cast<size_t>(targetSlot)] >= 0;
        const bool continues = loop.throws[static_cast<size_t>(targetSlot)].value == kOpenThrow || target == edit.drawFrom;
        if (taken)
            ImGui::SetTooltip("%s Something already lands here: you'll pick it up\n"
                              "and find it a new home next.", what.c_str());
        else if (continues)
            ImGui::SetTooltip("%s Then click where it goes next (Esc stops).", what.c_str());
        else
            ImGui::SetTooltip("%s Joins the path that carries on from here: done.", what.c_str());
    } else if (hoverInbound && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("Nothing lands here yet. Click to draw a throw that's caught here,\n"
                          "then click where it's thrown from.");
    } else if (hoverOpen && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("Not decided yet. Click to draw a throw from here.");
    } else if (sketch && hoverIndex >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("Click to draw this throw again.\nRight-click to delete it or its whole path.");
    }

    // The right-click menu.
    if (ImGui::BeginPopup(kContextMenu)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        std::vector<int> highlight;
        auto reasonTooltip = [](const std::string& text) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !text.empty())
                ImGui::SetTooltip("%s", text.c_str());
        };
        if (edit.context == LadderEditState::Context::Throw) {
            const int slot = loopSlot(edit.contextSlot);
            const LoopThrow t = loop.throws[static_cast<size_t>(slot)];
            // Deleting works at the props' cycle, so it takes out just this prop's throw (and
            // its copies, where the same prop throws it again): in the cascade "3", one throw in
            // three, not all of them. (A sketch's paths are already its props.)
            const int cyclePeriod = propCyclePeriod(loop);
            const JugglingLoop cycleLoop = cyclePeriod == period ? loop : loopWithPeriod(loop, cyclePeriod);
            const int cycleSlot = edit.contextSlot.juggler * cyclePeriod + positiveMod(edit.contextSlot.beat, cyclePeriod);
            ImGui::TextDisabled("J%d's %s on beat %d", edit.contextSlot.juggler + 1,
                                throwLabel(t, edit.contextSlot.juggler, jugglers, options.relativeTargets).c_str(), edit.contextSlot.beat + 1);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete throw")) {
                result.committed = true;
                result.loop = deleteThrow(cycleLoop, cycleSlot);
            }
            reasonTooltip("Leaves the throw undecided (?), to be drawn again.");
            if (ImGui::MenuItem("Delete path")) {
                result.committed = true;
                result.loop = deletePath(cycleLoop, cycleSlot);
            }
            if (ImGui::IsItemHovered()) {
                highlight = loopPathSlots(cycleLoop, cycleSlot);
                edit.pathHighlightPeriod = cyclePeriod;
                ImGui::SetTooltip("Leaves every throw on this path undecided (%d throw%s).",
                                  static_cast<int>(highlight.size()), highlight.size() == 1 ? "" : "s");
            }
        } else if (edit.context == LadderEditState::Context::Juggler) {
            const int j = std::min(edit.contextJuggler, jugglers - 1);
            if (isLinked(j)) {
                const PartLink& link = form->links[static_cast<size_t>(j)];
                ImGui::TextDisabled("J%d: %s", j + 1, linkWords(link.to, link.start, period).c_str());
            } else {
                ImGui::TextDisabled("J%d", j + 1);
            }
            ImGui::Separator();
            // Same as...: another juggler, starting from one of their beats. Starts that wouldn't
            // make a pattern (or sketch) are greyed out, saying why.
            const PatternForm emptyForm;
            if (ImGui::BeginMenu("Same as", jugglers > 1)) {
                for (int k = 0; k < jugglers; ++k) {
                    if (k == j) continue;
                    char label[16];
                    std::snprintf(label, sizeof(label), "J%d", k + 1);
                    if (!ImGui::BeginMenu(label)) continue;
                    for (int d = 0; d < period; ++d) {
                        PatternForm newForm;
                        JugglingLoop newLoop;
                        std::string why;
                        const bool ok = linkJuggler(form ? *form : emptyForm, loop, j, k, d, &newForm, &newLoop, &why);
                        // "From beat 5, 3p+2 (@1[4])": where J(j) starts in J(k)'s throws.
                        const std::string what = "From beat " + std::to_string(d + 1) + ", " +
                                                 throwLabel(loop.at(k, d), k, jugglers, options.relativeTargets) + " (" +
                                                 linkText(k, d, period) + ")";
                        char item[64];
                        std::snprintf(item, sizeof(item), "%s", what.c_str());
                        if (ImGui::MenuItem(item, nullptr, false, ok)) {
                            result.linkJuggler = j;
                            result.linkTo = k;
                            result.linkStart = d;
                        }
                        reasonTooltip(ok ? std::string() : "Can't: " + why);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make J%d do exactly what another juggler does, starting from one of their beats\n"
                                  "(a link). Editing either one then edits both.",
                                  j + 1);
            if (ImGui::MenuItem("Unlink", nullptr, false, isLinked(j))) result.unlinkJuggler = j;
            reasonTooltip(isLinked(j) ? "Writes J" + std::to_string(j + 1) + "'s throws out in full, to be edited on their own."
                                      : "J" + std::to_string(j + 1) + " isn't a link.");
        } else {
            const int beat = edit.contextBeat;
            ImGui::TextDisabled("Beat %d", beat + 1);
            ImGui::Separator();
            auto insertItem = [&](const char* label, int before, int count) {
                JugglingLoop changed;
                std::string why;
                const bool ok = !(form && form->hasLinks()) && insertBeats(loop, before, count, &changed, &why);
                if (form && form->hasLinks()) why = "not with linked jugglers yet. Unlink them first (right-click a juggler's number).";
                if (ImGui::MenuItem(label, nullptr, false, ok)) {
                    result.committed = true;
                    result.loop = changed;
                }
                std::string text;
                if (!ok) {
                    text = "Can't: " + why;
                } else {
                    std::string newText;
                    loopToText(changed, &newText);
                    text = "Throws in the air across the new beat" + std::string(count == 1 ? "" : "s") +
                           " get longer; the new beat" + (count == 1 ? " is" : "s are") + " empty (0).\nResult: " + newText;
                    if (count % 2 == 1) text += "\nSwaps left and right hands for the rest of the loop.";
                }
                reasonTooltip(text);
            };
            auto deleteItem = [&](const char* label, int first, int count) {
                JugglingLoop changed;
                std::string why;
                int removed = 0;
                const bool ok = !(form && form->hasLinks()) && deleteBeats(loop, first, count, &changed, &why, &removed);
                if (form && form->hasLinks()) why = "not with linked jugglers yet. Unlink them first (right-click a juggler's number).";
                if (ImGui::MenuItem(label, nullptr, false, ok)) {
                    result.committed = true;
                    result.loop = changed;
                }
                std::string text;
                if (!ok) {
                    text = "Can't: " + why;
                } else {
                    std::string newText;
                    loopToText(changed, &newText);
                    text = "A prop caught on a deleted beat goes straight on to its next throw.\nResult: " + newText;
                    if (removed > 0)
                        text += "\nRemoves " + std::to_string(removed) + " prop" + (removed == 1 ? "" : "s") +
                                " (their whole route was on the deleted beat" + (count == 1 ? ")." : "s).");
                    if (count % 2 == 1) text += "\nSwaps left and right hands for the rest of the loop.";
                }
                reasonTooltip(text);
            };
            insertItem("Add a beat above", beat, 1);
            insertItem("Add two beats above", beat, 2);
            insertItem("Add a beat below", beat + 1, 1);
            insertItem("Add two beats below", beat + 1, 2);
            ImGui::Separator();
            deleteItem("Delete this beat", beat, 1);
            deleteItem("Delete this beat and the next", beat, 2);
        }
        edit.pathHighlight = highlight;
        ImGui::EndPopup();
    } else {
        edit.pathHighlight.clear();
    }
    if (result.committed && result.loop.period > kMaxLoopBeats) {
        result.committed = false;  // too long to keep: refuse quietly (the menu said the result)
        result.loop = JugglingLoop();
    }

    dl->PopClipRect();  // the body

    // Header line.
    {
        std::string header;
        if (chain.active && chain.end == HeldEnd::Arrival)
            header = "Holding a throw: click where it should land. Esc or right-click cancels.";
        else if (chain.active)
            header = "Holding a throw: click where it should be thrown from. "
                     "Esc or right-click cancels.";
        else if (edit.drawing && edit.drawEnd == HeldEnd::Departure)
            header = "Holding a throw by its start: click where it should be thrown from. Esc or right-click stops.";
        else if (edit.drawing)
            header = "Drawing: click where the prop is caught next. Esc or right-click stops.";
        else if (sketch) {
            const int open = openThrowCount(loop);
            header = "Sketch: " + std::to_string(open) + " throw" + (open == 1 ? "" : "s") +
                     " not decided yet (?). Click one to draw from it.";
        } else
            header = std::to_string(loopPropCount(loop)) + " props, period " + std::to_string(period) +
                     ".  Click near either end of a throw to edit it.";
        dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f),
                    (chain.active || edit.drawing) ? textCol : dimText, header.c_str());
    }

    // Legend: one sample per prop (or per orbit, or per path in a sketch), so identity never
    // depends on color alone.
    {
        float x = origin.x + margin;
        const float y = origin.y + margin * 0.5f + fontSize * 1.9f;
        const float sampleLength = fontSize * 2.6f;
        const bool byOrbit = sketch || options.colorByOrbit;
        const int entries = byOrbit ? loopOrbits.count : totalBalls;
        for (int i = 0; i < entries; ++i) {
            const BallStyle style = ballStyle(colorVision, i);
            std::string label = std::to_string(i + 1);
            if (byOrbit && !sketch) {
                const int props = loopOrbits.propsOfOrbit[static_cast<size_t>(i)];
                label = "orbit " + label + " (" + std::to_string(props) + ")";
            } else if (sketch) {
                label = "path " + label;
            }
            const float width = markerRadius + sampleLength + fontSize * 0.3f + ImGui::CalcTextSize(label.c_str()).x;
            if (x + width > maxPt.x - margin && i > 0) {
                dl->AddText(ImVec2(x, y - fontSize * 0.5f), textCol, "...");
                break;
            }
            const ImVec2 a(x + markerRadius, y);
            const ImVec2 b(x + markerRadius + sampleLength, y);
            drawStyledLine(dl, a, b, style.color, thickness, style.dash, dashUnit);
            drawMarker(dl, a, markerRadius, style.shape, style.color, 2.0f, kCanvasBackground);
            dl->AddText(ImVec2(b.x + fontSize * 0.3f, y - fontSize * 0.5f), textCol, label.c_str());
            x = b.x + fontSize * 0.3f + ImGui::CalcTextSize(label.c_str()).x + fontSize * 1.0f;
        }
    }

    // Strip headers: the juggler's number (with several jugglers; the selected one is drawn
    // dark on light, as over their head in the 3D view), then L and R over the columns.
    {
        const float handY = bodyTop - fontSize * 1.25f;
        for (int j = 0; j < jugglers; ++j) {
            if (jugglers > 1) {
                ImVec2 a, b;
                jugglerHeaderRect(j, &a, &b);
                const bool selected = j == options.selectedJuggler;
                const bool hovered = j == hoverHeader && !hoverToggle;
                const float rounding = fontSize * 0.3f;
                // The collapse button: its own small outlined box, the triangle pointing down
                // while expanded and right while collapsed.
                {
                    ImVec2 ta, tb;
                    collapseToggleRect(j, &ta, &tb);
                    const bool toggleHovered = j == hoverHeader && hoverToggle;
                    dl->AddRectFilled(ta, tb, toggleHovered ? IM_COL32(64, 70, 86, 255) : IM_COL32(22, 24, 30, 255), rounding);
                    dl->AddRect(ta, tb, toggleHovered ? IM_COL32(200, 204, 214, 230) : IM_COL32(110, 116, 132, 200), rounding, 0, 1.0f);
                    const float tx = 0.5f * (ta.x + tb.x), ty = 0.5f * (ta.y + tb.y);
                    const float r = fontSize * 0.26f;
                    const ImU32 arrow = toggleHovered ? IM_COL32(240, 242, 248, 255) : IM_COL32(170, 176, 190, 255);
                    if (isCollapsed(j))
                        dl->AddTriangleFilled(ImVec2(tx - r * 0.6f, ty - r), ImVec2(tx - r * 0.6f, ty + r), ImVec2(tx + r * 0.8f, ty), arrow);
                    else
                        dl->AddTriangleFilled(ImVec2(tx - r, ty - r * 0.6f), ImVec2(tx + r, ty - r * 0.6f), ImVec2(tx, ty + r * 0.8f), arrow);
                }
                dl->AddRectFilled(a, b, selected ? IM_COL32(236, 238, 244, 255)
                                                 : (hovered ? IM_COL32(52, 56, 68, 255) : IM_COL32(28, 30, 36, 255)),
                                  rounding);
                dl->AddRect(a, b, IM_COL32(210, 214, 224, 230), rounding, 0, selected ? 2.0f : 1.0f);
                const ImU32 ink = selected ? IM_COL32(16, 16, 20, 255) : IM_COL32(232, 234, 240, 255);
                const std::string label = "J" + std::to_string(j + 1);
                const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), ink, label.c_str());
                // A link says what it copies ("= J2[3]"), if there's room.
                if (isLinked(j) && !isCollapsed(j)) {
                    const PartLink& link = form->links[static_cast<size_t>(j)];
                    std::string what = "= J" + linkText(link.to, link.start, period).substr(1);
                    if (link.lrSwap) what += " LR";
                    // Between the L and R labels below the number, where there's room.
                    const ImVec2 ws = ImGui::CalcTextSize(what.c_str());
                    const float left = columnX(j, false) + fontSize * 0.6f, right = columnX(j, true) - fontSize * 0.6f;
                    if (ws.x <= right - left)
                        dl->AddText(ImVec2(0.5f * (left + right - ws.x), bodyTop - fontSize * 1.25f), textCol, what.c_str());
                }
            }
            if (isCollapsed(j)) continue;  // no room for L and R
            for (int right = 0; right < 2; ++right) {
                const char* label = right ? "R" : "L";
                const float x = columnX(j, right != 0);
                dl->AddText(ImVec2(x - ImGui::CalcTextSize(label).x * 0.5f, handY), textCol, label);
            }
        }
        if (hoverHeader >= 0 && hoverToggle && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            ImGui::SetTooltip(isCollapsed(hoverHeader) ? "Expand juggler %d's strip\n(C: expand or collapse all)"
                                                       : "Collapse juggler %d's strip\n(C: expand or collapse all)",
                              hoverHeader + 1);
        else if (hoverHeader >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen(kContextMenu)) {
            std::string what = "Juggler " + std::to_string(hoverHeader + 1);
            if (isLinked(hoverHeader)) {
                const PartLink& link = form->links[static_cast<size_t>(hoverHeader)];
                what += ": " + linkWords(link.to, link.start, period);
                if (link.lrSwap) what += ", hands swapped";
            }
            what += hoverHeader == options.selectedJuggler ? " (selected). Click to deselect." : ". Click to select.";
            what += "\nRight-click: Same as... / Unlink";
            ImGui::SetTooltip("%s", what.c_str());
        }
    }

    dl->PopClipRect();
    return result;
}
