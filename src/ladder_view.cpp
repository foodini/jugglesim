// ladder_view.cpp - see ladder_view.h.
#include "ladder_view.h"

#include "draw_helpers.h"
#include "imgui.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
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

LadderToolbarRequest drawLadderToolbar(const Pattern& pattern, bool patternValid,
                                       const LadderEditState& edit) {
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
        ImGui::SetTooltip("Back to the default pan and zoom, with beat 1 at the left (Home, or Ctrl+0).\n\n"
                          "On the ladder: mouse wheel pans, Ctrl+wheel zooms.");

    return request;
}

namespace {

// Geometry of one throw curve on the ladder.
struct Curve {
    ImVec2 p0, c1, c2, p1;
};

// A throw as drawn this frame (one instance of a loop slot).
struct DrawnThrow {
    int beat;   // departure beat
    int value;  // siteswap value; arrival beat = beat + value
    int ball;   // ball id (index into the ball styles)
    Curve curve;
};

// What the mouse is over: a throw end (idle) or a drop target beat (holding).
struct HitResult {
    int index = -1;  // index into the DrawnThrow list, or -1
    float t = 0.0f;  // curve parameter of the nearest point
    float distance = 1e9f;
};

}  // namespace

void resetLadderView(LadderEditState& edit) {
    edit.firstBeat = 0.0f;
    edit.zoom = 1.0f;
}

bool ladderViewIsDefault(const LadderEditState& edit) {
    return edit.firstBeat == 0.0f && edit.zoom == 1.0f;
}

void cancelLadderEdit(LadderEditState& edit) {
    edit.chain = EditChain();
    edit.hoverBeat = INT_MIN;
    edit.hoverTarget = INT_MIN;
}

LadderEditResult drawLadderDiagram(const Pattern& pattern, bool patternValid,
                                   ColorVisionMode colorVision, LadderEditState& edit,
                                   double playheadBeat) {
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
    const float x0 = origin.x + margin * 3.0f;  // where the left-most beat sits; room for R/L
    const float baseSpacing = fontSize * 3.0f;   // beat spacing at zoom 1

    // Pan and zoom. Wheel (or a horizontal trackpad swipe) pans; Ctrl+wheel zooms around the
    // mouse; Home or Ctrl+0 resets. firstBeat is the (fractional) beat drawn at x0.
    {
        ImGuiIO& io = ImGui::GetIO();
        if (canvasHovered && io.KeyCtrl && io.MouseWheel != 0.0f) {
            const float oldSpacing = baseSpacing * edit.zoom;
            const float mouseBeat = edit.firstBeat + (mouse.x - x0) / oldSpacing;
            edit.zoom = std::clamp(edit.zoom * std::pow(1.15f, io.MouseWheel), kMinLadderZoom,
                                   kMaxLadderZoom);
            edit.firstBeat = mouseBeat - (mouse.x - x0) / (baseSpacing * edit.zoom);
        } else if (canvasHovered && !io.KeyCtrl) {
            // Wheel up / swipe left moves toward earlier beats. ~2 beats per notch, scaled so
            // the same distance on screen moves at any zoom.
            const float beatsPerNotch = 2.0f / edit.zoom;
            edit.firstBeat -= (io.MouseWheel + io.MouseWheelH) * beatsPerNotch;
        }
        // Only while the mouse is over the ladder: Home over the juggler resets the camera.
        if (ImGui::IsWindowHovered() && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Home) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0))))
            resetLadderView(edit);
    }
    const float beatSpacing = baseSpacing * edit.zoom;
    const float firstBeat = edit.firstBeat;
    // Beats with any part on screen (a little margin either side for markers and labels).
    const int bFirst = static_cast<int>(std::floor(firstBeat - (x0 - origin.x) / beatSpacing)) - 1;
    const int bLast = static_cast<int>(std::ceil(firstBeat + (maxPt.x - x0) / beatSpacing)) + 1;
    // Remember whether beat 1 was visible, for the toolbar's Reset View button.
    edit.beatOneVisible = (x0 + (0.0f - firstBeat) * beatSpacing >= origin.x + margin * 2.0f) &&
                          (x0 + (0.0f - firstBeat) * beatSpacing <= maxPt.x);

    auto beatX = [&](int b) { return x0 + (static_cast<float>(b) - firstBeat) * beatSpacing; };
    // Beat 0 (and every even beat) is the right hand. Works for negative beats too.
    auto railY = [&](int b) { return positiveMod(b, 2) == 0 ? railTopY : railBotY; };
    auto beatPoint = [&](int b) { return ImVec2(beatX(b), railY(b)); };

    // Curve for a throw of value v made on beat b.
    auto throwCurve = [&](int b, int v) {
        Curve c;
        c.p0 = beatPoint(b);
        c.p1 = beatPoint(b + v);
        const float dx = c.p1.x - c.p0.x;
        if (v % 2 != 0) {
            // Crossing throw: gentle S-curve from one rail to the other.
            c.c1 = ImVec2(c.p0.x + dx * 0.35f, c.p0.y);
            c.c2 = ImVec2(c.p1.x - dx * 0.35f, c.p1.y);
        } else {
            // Same-hand throw: arch outside the ladder (right hand above the top rail, left hand
            // below the bottom one), taller for higher throws, reaching the edge of the
            // available room at 8.
            const float bulge = outsideRoom * 0.9f * std::min(1.0f, static_cast<float>(v) / 8.0f);
            const float dir = (c.p0.y == railTopY) ? -1.0f : 1.0f;
            const float cy = c.p0.y + dir * bulge * (4.0f / 3.0f);  // cubic peak ~= bulge
            c.c1 = ImVec2(c.p0.x + dx * 0.1f, cy);
            c.c2 = ImVec2(c.p1.x - dx * 0.1f, cy);
        }
        return c;
    };
    // Rubber band from a fixed point to the mouse when it isn't over a valid target.
    auto looseCurve = [&](ImVec2 from, ImVec2 to) {
        Curve c;
        c.p0 = from;
        c.p1 = to;
        const float dx = to.x - from.x;
        c.c1 = ImVec2(from.x + dx * 0.35f, from.y);
        c.c2 = ImVec2(to.x - dx * 0.35f, to.y);
        return c;
    };

    const ImU32 railCol = IM_COL32(150, 155, 170, 255);
    const ImU32 rungRightCol = IM_COL32(110, 116, 132, 255);
    const ImU32 rungLeftCol = IM_COL32(58, 62, 74, 255);
    const ImU32 textCol = IM_COL32(150, 155, 170, 255);
    const ImU32 dimText = IM_COL32(100, 104, 116, 255);
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 ghostColor = IM_COL32(170, 172, 180, 255);  // a held empty beat (a 0)

    std::vector<int> loop = patternValid ? loopThrowValues(pattern) : std::vector<int>();
    if (loop.empty() && edit.chain.active) cancelLadderEdit(edit);
    EditChain& chain = edit.chain;
    const int period = static_cast<int>(loop.size());
    const bool shiftHeld = ImGui::GetIO().KeyShift;

    // Background: alternate shades every loop period, so the repeat stands out. Plain grays,
    // independent of color-vision mode, kept very dark so throw colors keep their contrast.
    if (period > 0) {
        const ImU32 periodShades[2] = {kCanvasBackground, IM_COL32(24, 26, 32, 255)};
        const float top = origin.y + headerHeight;
        for (int b = bFirst; b <= bLast; ++b) {
            const float left = beatX(b) - beatSpacing * 0.5f;
            const int periodIndex = b >= 0 ? b / period : -((-b + period - 1) / period);
            dl->AddRectFilled(ImVec2(left, top), ImVec2(left + beatSpacing, maxPt.y),
                              periodShades[positiveMod(periodIndex, 2)]);
        }
    }

    // Beat lines, rails, labels, and beat numbers. When zoomed out, only every n-th beat is
    // numbered so the numbers don't collide (always including beat 1).
    const float widestLabel = ImGui::CalcTextSize("-000").x + fontSize * 0.5f;
    const int labelStep = std::max(1, static_cast<int>(std::ceil(widestLabel / beatSpacing)));
    for (int b = bFirst; b <= bLast; ++b) {
        // Beat line: from the top of the right hand's arch space down to just above the beat
        // number, so any point on a throw can be traced straight down to its beat.
        // Right-hand beats (1, 3, 5, ...) get a brighter, heavier line than left-hand beats, so
        // a throw's hand can be read from the line it starts on.
        const bool rightHandBeat = (b % 2 == 0);
        dl->AddLine(ImVec2(beatX(b), railTopY - outsideRoom),
                    ImVec2(beatX(b), maxPt.y - numbersHeight + fontSize * 0.15f),
                    rightHandBeat ? rungRightCol : rungLeftCol, rightHandBeat ? 2.0f : 1.0f);
        if (positiveMod(b, labelStep) != 0) continue;
        const std::string label = std::to_string(b + 1);  // beat 0 is "beat 1"
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(beatX(b) - ts.x * 0.5f, maxPt.y - numbersHeight + fontSize * 0.3f),
                    dimText, label.c_str());
    }
    const float railStart = x0 - margin;
    dl->AddLine(ImVec2(railStart, railTopY), ImVec2(maxPt.x, railTopY), railCol, 2.0f);
    dl->AddLine(ImVec2(railStart, railBotY), ImVec2(maxPt.x, railBotY), railCol, 2.0f);

    if (period == 0) {
        const char* msg = "No valid pattern to display. Enter a siteswap below.";
        dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f), dimText, msg);
        dl->PopClipRect();
        return result;
    }

    // Throw value on a beat. While a chain is open, its local edits are applied (and the held
    // throw's departure reads kNoThrow).
    auto valueAt = [&](int beat) {
        return chain.active ? editValueAt(chain, beat)
                            : loop[static_cast<size_t>(positiveMod(beat, period))];
    };

    // ---- Ball identity (for colors) ----------------------------------------------------------
    // In a repeating pattern each ball follows a fixed cycle ("orbit") through the loop's
    // throws, so a throw's ball can be worked out from its beat alone. Ids therefore don't
    // depend on what part of the ladder is on screen, and panning never recolors anything.
    const BallOrbits orbits = computeBallOrbits(loop);
    const int totalBalls = orbits.totalBalls;
    auto orbitBallAt = [&](int b) { return ::orbitBallAt(orbits, b); };

    // While a chain is open, follow the balls through the edit: start well before anything the
    // chain touched (where the pattern is unedited, so orbit ids apply), and track landings
    // forward. The held throw is treated as if it had gone back to the empty spot, so it keeps
    // its own ball and nothing else shares its color.
    auto virtualAt = [&](int beat) {
        if (chain.active) {
            if (chain.end == HeldEnd::Arrival && beat == chain.fixedBeat)
                return std::max(0, chain.holeBeat - chain.fixedBeat);
            if (chain.end == HeldEnd::Departure && beat == chain.holeBeat)
                return std::max(0, chain.fixedBeat - chain.holeBeat);
        }
        return valueAt(beat);
    };
    std::unordered_map<int, int> chainBallOfBeat;  // departure beat -> ball id
    if (chain.active) {
        int lo = std::min(chain.fixedBeat, chain.holeBeat);
        if (!chain.overrides.empty()) lo = std::min(lo, chain.overrides.begin()->first);
        const int simStart = std::min(bFirst - 40, lo - 40);
        std::unordered_map<int, int> arriving;  // landing beat -> ball id
        for (int d = simStart - 36; d < simStart; ++d) {  // balls already in the air
            const int v = loop[static_cast<size_t>(positiveMod(d, period))];
            if (v > 0 && d + v >= simStart) arriving[d + v] = orbitBallAt(d);
        }
        for (int b = simStart; b <= bLast; ++b) {
            const int t = virtualAt(b);
            if (t <= 0) continue;
            std::unordered_map<int, int>::iterator it = arriving.find(b);
            const int ball = it != arriving.end() ? it->second : orbitBallAt(b);
            if (it != arriving.end()) arriving.erase(it);
            chainBallOfBeat[b] = ball;
            arriving[b + t] = ball;
        }
    }
    auto ballAt = [&](int b) {
        if (!chain.active) return orbitBallAt(b);
        std::unordered_map<int, int>::const_iterator it = chainBallOfBeat.find(b);
        return it != chainBallOfBeat.end() ? it->second : -1;
    };
    const int nextBall = totalBalls;  // number of balls in the legend
    if (chain.active) {
        const int heldBeat = chain.end == HeldEnd::Arrival ? chain.fixedBeat : chain.holeBeat;
        edit.heldBall = chain.heldValue == 0 ? -1 : ballAt(heldBeat);
    }

    // The throws drawn this frame.
    // Includes throws made before the left edge that are still in the air on screen.
    std::vector<DrawnThrow> drawn;
    for (int b = bFirst - 36; b <= bLast; ++b) {
        const int t = valueAt(b);
        if (t <= 0 || b + t < bFirst) continue;
        drawn.push_back({b, t, std::max(0, ballAt(b)), throwCurve(b, t)});
    }

    // ---- Interaction -------------------------------------------------------------------------
    const float hitRadius = std::max(6.0f, fontSize * 0.5f);
    const float ambiguityMargin = 3.0f;  // the nearest curve must win by this much
    const float stickyRadius = hitRadius * 1.6f;  // keep the current hover while within this
    const float snapRadius = beatSpacing * 0.45f;  // grabbing a beat's point on the rail

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

    if (canvasHovered && (rightClicked || ImGui::IsKeyPressed(ImGuiKey_Escape)) && chain.active)
        cancelLadderEdit(edit);

    // Idle hover: which end of which throw would a click pick up?
    int hoverIndex = -1;
    HeldEnd hoverEnd = HeldEnd::Arrival;
    int hoverZeroBeat = INT_MIN;  // an empty beat (0) under the mouse
    // Holding: which beat would a click drop the held end on?
    int targetBeat = INT_MIN;

    if (canvasHovered && !chain.active) {
        const HitResult hit = pickThrow([&](const DrawnThrow& d, float t) {
            const HeldEnd e = t < 0.5f ? HeldEnd::Departure : HeldEnd::Arrival;
            return d.beat == edit.hoverBeat && e == edit.hoverEnd;
        }, [](float) { return true; });
        if (hit.index >= 0) {
            const DrawnThrow& d = drawn[static_cast<size_t>(hit.index)];
            hoverIndex = hit.index;
            // Small dead zone around the midpoint so the chosen half doesn't flicker.
            if (d.beat == edit.hoverBeat && std::fabs(hit.t - 0.5f) < 0.06f)
                hoverEnd = edit.hoverEnd;
            else
                hoverEnd = hit.t < 0.5f ? HeldEnd::Departure : HeldEnd::Arrival;
        } else {
            // Empty beats can be picked up too (they "land" on their own beat).
            float bestD = fontSize * 0.6f;
            for (int b = bFirst; b <= bLast; ++b) {
                if (valueAt(b) != 0) continue;
                const ImVec2 p = beatPoint(b);
                const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                if (d < bestD) {
                    bestD = d;
                    hoverZeroBeat = b;
                }
            }
        }
        edit.hoverBeat = hoverIndex >= 0 ? drawn[static_cast<size_t>(hoverIndex)].beat : INT_MIN;
        edit.hoverEnd = hoverEnd;

        if (leftClicked && hoverIndex >= 0) {
            const DrawnThrow& d = drawn[static_cast<size_t>(hoverIndex)];
            chain = beginEditChain(loop, d.beat, hoverEnd);
            result.startedChain = true;
        } else if (leftClicked && hoverZeroBeat != INT_MIN) {
            chain = beginEditChain(loop, hoverZeroBeat, HeldEnd::Arrival);
            result.startedChain = true;
        }
    } else if (canvasHovered && chain.active) {
        // Near a beat's point on the rail, that beat is the target: several curves meet there,
        // and the beat under the mouse is what the user means. Elsewhere, a curve under the
        // mouse targets its relevant end (its landing for a held arrival, its start for a held
        // departure), and failing that the nearest beat point within snapping distance.
        const bool arrivalMode = chain.end == HeldEnd::Arrival;
        const float pointPriorityRadius = fontSize * 0.9f;
        int nearestBeat = INT_MIN;
        float nearestD = 1e9f;
        for (int b = bFirst; b <= bLast; ++b) {
            const ImVec2 p = beatPoint(b);
            const float dd = std::hypot(p.x - mouse.x, p.y - mouse.y);
            if (dd < nearestD) {
                nearestD = dd;
                nearestBeat = b;
            }
        }
        if (nearestD <= pointPriorityRadius) {
            targetBeat = nearestBeat;
        } else {
            // Only the matching half of other throws counts: a held catch is dropped by pointing
            // at another throw's incoming (catch) half, a held throw end at an outgoing half.
            // Otherwise a throw leaving the beat you're aiming at would steal the drop.
            const HitResult hit = pickThrow(
                [&](const DrawnThrow& d, float) {
                    return (arrivalMode ? d.beat + d.value : d.beat) == edit.hoverTarget;
                },
                [&](float t) { return arrivalMode ? t >= 0.5f : t <= 0.5f; });
            if (hit.index >= 0) {
                const DrawnThrow& d = drawn[static_cast<size_t>(hit.index)];
                targetBeat = arrivalMode ? d.beat + d.value : d.beat;
            } else if (nearestD <= snapRadius) {
                targetBeat = nearestBeat;
            }
        }
        if (targetBeat != INT_MIN && !editDropValue(chain, targetBeat, nullptr))
            targetBeat = INT_MIN;  // not a legal drop: the rubber band just follows the mouse
        edit.hoverTarget = targetBeat;

        if (leftClicked && targetBeat != INT_MIN) {
            std::vector<int> newLoop;
            const DropResult r = editDrop(chain, targetBeat, shiftHeld, &newLoop);
            if (r == DropResult::Closed) {
                result.committed = true;
                result.loop = newLoop;
                cancelLadderEdit(edit);
            }
        }
    }

    // ---- Drawing -----------------------------------------------------------------------------
    const float thickness = 2.5f;
    const float arrowSize = fontSize * 0.55f;
    const float markerRadius = fontSize * 0.36f;
    const float dashUnit = fontSize * 0.35f;

    // Header line.
    {
        std::string header;
        if (chain.active && chain.end == HeldEnd::Arrival)
            header = "Holding a throw: click where it should land. Esc or right-click cancels.";
        else if (chain.active)
            header = "Holding a throw: click the beat it should be thrown from. "
                     "Esc or right-click cancels.";
        else
            header = "Ladder diagram: " + std::to_string(ballCount(pattern)) + " objects, period " +
                     std::to_string(period) + ".  Click near either end of a throw to edit.";
        dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f),
                    chain.active ? textCol : dimText, header.c_str());
    }

    // Legend: one sample per ball, so identity never depends on color alone.
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

    // Throws: curves and arrowheads.
    for (const DrawnThrow& d : drawn) {
        const BallStyle style = ballStyle(colorVision, d.ball);
        const Curve& c = d.curve;
        drawStyledBezier(dl, c.p0, c.c1, c.c2, c.p1, style.color, thickness, style.dash, dashUnit);
        drawBezierArrowHead(dl, c.p0, c.c1, c.c2, c.p1, style.color, arrowSize, markerRadius + 3.0f);
    }

    // Idle hover highlight: just the throw under the mouse.
    if (hoverIndex >= 0) {
        const DrawnThrow& h = drawn[static_cast<size_t>(hoverIndex)];
        const Curve& c = h.curve;
        drawBezierHalfHighlight(dl, c.p0, c.c1, c.c2, c.p1, hoverEnd == HeldEnd::Departure,
                                ballStyle(colorVision, h.ball).color, 1.0f);
    }
    if (hoverZeroBeat != INT_MIN)
        dl->AddCircle(beatPoint(hoverZeroBeat), 9.0f, mixColor(ghostColor, white, 0.5f, 0.9f), 0, 2.0f);

    // Holding: the empty spot(s), the rubber band and its repeats.
    if (chain.active) {
        const bool arrivalMode = chain.end == HeldEnd::Arrival;
        const float pulse = 0.5f + 0.5f * std::sin(time * 5.0f);

        // The empty spot that closes the chain: pulsing circles. Its copies in other repeats
        // that the chain could close on instead (Shift-click, changing the ball count) get
        // pulsing squares, dim normally and bright while Shift is held.
        for (int b = bFirst; b <= bLast; ++b) {
            if (positiveMod(b - chain.holeBeat, period) != 0) continue;
            const ImVec2 p = beatPoint(b);
            if (b == chain.holeBeat) {
                dl->AddCircle(p, markerRadius + 5.0f + 2.0f * pulse, mixColor(white, white, 0.0f, 0.95f), 0, 2.0f);
                dl->AddCircle(p, markerRadius + 1.5f, mixColor(white, white, 0.0f, 0.57f), 0, 1.5f);
            } else if (editCloseLoopLength(chain, b, true) > 0) {
                const float a = shiftHeld ? 0.95f : 0.35f;
                const float outer = markerRadius + 4.0f + 2.0f * pulse;
                const float inner = markerRadius + 1.5f;
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
            const int fixedB = chain.fixedBeat + shift;
            if (targetBeat != INT_MIN) {
                const int target = targetBeat + shift;
                return arrivalMode ? throwCurve(fixedB, target - fixedB) : throwCurve(target, fixedB - target);
            }
            const ImVec2 fixedP = beatPoint(fixedB);
            const ImVec2 loose(mouse.x + static_cast<float>(shift) * beatSpacing, mouse.y);
            return arrivalMode ? looseCurve(fixedP, loose) : looseCurve(loose, fixedP);
        };

        // Dull impressions of the edit in the nearest few repeats, fading with distance. Only
        // while the mouse is over a valid drop target, so they show where the edit would go
        // rather than chasing the mouse. Skipped for a 1-beat spacing: then every throw is a
        // copy, which says nothing useful.
        if (targetBeat != INT_MIN && echoSpacing >= 2) {
            for (int m = -3; m <= 3; ++m) {
                if (m == 0) continue;
                const Curve c = heldCurve(m);
                if (std::max(c.p0.x, c.p1.x) < origin.x || std::min(c.p0.x, c.p1.x) > maxPt.x) continue;
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
        if (targetBeat != INT_MIN) {
            drawBezierArrowHead(dl, c.p0, c.c1, c.c2, c.p1, heldStyle.color, arrowSize, markerRadius + 3.0f);
            // Highlight the target the way a hovered end is highlighted.
            drawBezierHalfHighlight(dl, c.p0, c.c1, c.c2, c.p1, !arrivalMode, heldStyle.color, 1.0f);
        } else {
            dl->AddCircleFilled(arrivalMode ? c.p1 : c.p0, 4.0f, mixColor(heldStyle.color, white, 0.5f, 0.9f));
        }
    }

    // Playhead: where the juggler is now, at sub-beat resolution, plus the same moment in every
    // other repeat of the loop that's on screen (fainter). Positioned with floats so it glides.
    {
        const float top = railTopY - outsideRoom;
        const float bottom = maxPt.y - numbersHeight + fontSize * 0.15f;
        const double localBeat = static_cast<double>(firstBeat);
        const double rel = playheadBeat - localBeat;
        // Copies at playheadBeat + k*period; draw every one that lands on screen. The left-most
        // visible copy is the bright one, so there's always exactly one to follow.
        const double firstCopy = playheadBeat - std::floor(rel / period) * period - period;
        bool primaryDrawn = false;
        for (double b = firstCopy; ; b += period) {
            const float x = x0 + static_cast<float>(b - localBeat) * beatSpacing;
            if (x > maxPt.x + 2.0f) break;
            if (x < origin.x + margin * 2.0f) continue;
            const bool primary = !primaryDrawn;
            primaryDrawn = true;
            const ImU32 col = primary ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 255, 255, 90);
            if (primary) dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), IM_COL32(255, 255, 255, 40), 6.0f);
            dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), col, primary ? 2.0f : 1.0f);
            // A small downward-pointing cap, so playhead copies can't be mistaken for beat lines.
            const float cap = fontSize * (primary ? 0.45f : 0.35f);
            dl->AddTriangleFilled(ImVec2(x - cap, top - cap * 1.1f), ImVec2(x + cap, top - cap * 1.1f),
                                  ImVec2(x, top + cap * 0.2f), col);
        }
    }

    // Departure markers on top, ringed in the background color so they stay readable where
    // curves cross them. Empty beats get a small hollow circle.
    for (int b = bFirst; b <= bLast; ++b) {
        const int t = valueAt(b);
        const ImVec2 p0 = beatPoint(b);
        if (t == 0) {
            dl->AddCircle(p0, fontSize * 0.25f, dimText, 0, 1.5f);
        } else if (t > 0) {
            const BallStyle style = ballStyle(colorVision, std::max(0, ballAt(b)));
            drawMarker(dl, p0, markerRadius, style.shape, style.color, 2.0f, kCanvasBackground);
        }
    }
    if (chain.active) {
        // The held throw's departure marker (its departure reads kNoThrow while held).
        const bool ghost = edit.heldBall < 0;
        const BallStyle heldStyle = ghost ? BallStyle{ghostColor, MarkerShape::Circle, DashPattern::ShortDash}
                                          : ballStyle(colorVision, edit.heldBall);
        const int departure = chain.end == HeldEnd::Arrival
                                  ? chain.fixedBeat
                                  : (targetBeat != INT_MIN ? targetBeat : INT_MIN);
        if (departure != INT_MIN)
            drawMarker(dl, beatPoint(departure), markerRadius + 1.0f, heldStyle.shape, heldStyle.color, 2.0f,
                       kCanvasBackground);
    }

    // Tooltip describing what a click would do.
    if (chain.active && targetBeat != INT_MIN) {
        int v = 0;
        editDropValue(chain, targetBeat, &v);
        const int closeLength = editCloseLoopLength(chain, targetBeat, shiftHeld);
        const int wouldBeLength = editCloseLoopLengthUnlimited(chain, targetBeat, shiftHeld);
        const int copyLength =
            (!shiftHeld && targetBeat != chain.holeBeat) ? editCloseLoopLength(chain, targetBeat, true) : 0;
        if (closeLength == 0 && wouldBeLength > 0) {
            ImGui::SetTooltip("Can't close here: the loop would be %d beats long (the limit is %d).\n"
                              "Keep going and close somewhere that keeps the edit shorter,\n"
                              "or press Esc to cancel.",
                              wouldBeLength, kMaxLoopBeats);
        } else if (closeLength > 0) {
            const int delta = editDropBallDelta(chain, targetBeat);
            if (delta == 0)
                ImGui::SetTooltip("Becomes a %d. Fills the empty spot: done.\nThe edit repeats every %d beat%s.",
                                  v, closeLength, closeLength == 1 ? "" : "s");
            else
                ImGui::SetTooltip("Becomes a %d. Closes on a copy of the empty spot: %s%d ball%s.\n"
                                  "The edit repeats every %d beat%s.",
                                  v, delta > 0 ? "+" : "", delta, (delta == 1 || delta == -1) ? "" : "s",
                                  closeLength, closeLength == 1 ? "" : "s");
        } else if (copyLength > 0) {
            const int delta = editDropBallDelta(chain, targetBeat);
            ImGui::SetTooltip("Becomes a %d. Picks up the throw that was here.\n"
                              "(Shift-click to close here instead: %s%d ball%s.)",
                              v, delta > 0 ? "+" : "", delta, (delta == 1 || delta == -1) ? "" : "s");
        } else {
            ImGui::SetTooltip("Becomes a %d. Picks up the throw that was here.", v);
        }
    }

    // Left gutter with the R/L labels, drawn last so throws scrolled off to the left slide
    // under it instead of over the labels.
    dl->AddRectFilled(ImVec2(origin.x, origin.y + headerHeight),
                      ImVec2(origin.x + margin * 2.0f, maxPt.y), kCanvasBackground);
    dl->AddText(ImVec2(origin.x + margin * 0.6f, railTopY - fontSize * 0.5f), textCol, "R");
    dl->AddText(ImVec2(origin.x + margin * 0.6f, railBotY - fontSize * 0.5f), textCol, "L");

    dl->PopClipRect();
    return result;
}
