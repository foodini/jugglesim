// ladder_view.cpp - see ladder_view.h.
#include "ladder_view.h"

#include "draw_helpers.h"
#include "imgui.h"

#include <algorithm>
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

LadderToolbarRequest drawLadderToolbar(const Pattern& pattern, bool patternValid,
                                       const LadderEditState& edit, bool showValues) {
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

std::string throwLabel(const LoopThrow& t, int thrower, int jugglers) {
    std::string label(1, valueChar(t.value));
    if (t.dest != thrower) {
        label += 'p';
        if (jugglers > 2) label += std::to_string(t.dest + 1);
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
}

LadderEditResult drawLadderDiagram(const Pattern& pattern, bool patternValid,
                                   ColorVisionMode colorVision, LadderEditState& edit,
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

    const JugglingLoop loop = patternValid ? patternLoop(pattern) : JugglingLoop();
    if (loop.empty() && edit.chain.active) cancelLadderEdit(edit);
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
    const float jugglerCount = static_cast<float>(jugglers);
    const float stripGap = jugglers > 1 ? std::clamp((areaWidth - jugglerCount * fontSize * 12.0f) / (jugglerCount - 1.0f),
                                                     0.0f, fontSize * 5.0f)
                                        : 0.0f;
    const float stripWidth = std::min((areaWidth - stripGap * (jugglerCount - 1.0f)) / jugglerCount, fontSize * 22.0f);
    const float stripsWidth = stripWidth * jugglerCount + stripGap * (jugglerCount - 1.0f);
    const float stripsLeft = areaLeft + (areaWidth - stripsWidth) * 0.5f;
    const float stripsRight = stripsLeft + stripsWidth;
    const float outsideRoom = stripWidth * 0.25f;  // beside each column, for same-hand arches
    auto stripLeft = [&](int j) { return stripsLeft + (stripWidth + stripGap) * static_cast<float>(j); };
    auto columnX = [&](int j, bool right) {
        return stripLeft(j) + outsideRoom + (right ? stripWidth * 0.5f : 0.0f);
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
    auto rightHandBeat = [](int b) { return positiveMod(b, 2) == 0; };
    auto slotPoint = [&](Slot s) { return ImVec2(columnX(s.juggler, rightHandBeat(s.beat)), beatY(s.beat)); };

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
            const float bulge = outsideRoom * 0.9f * std::min(1.0f, static_cast<float>(t.value) / 8.0f);
            const float dir = rightHandBeat(from.beat) ? 1.0f : -1.0f;
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
            const bool right = rightHandBeat(b);
            dl->AddLine(ImVec2(stripLeft(j) + fontSize * 0.2f, y), ImVec2(stripLeft(j) + stripWidth - fontSize * 0.2f, y),
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
    // held throw's departure reads kNoThrow).
    auto throwAt = [&](Slot s) {
        return chain.active ? editThrowAt(chain, s) : loop.at(s.juggler, positiveMod(s.beat, period));
    };

    // ---- Ball identity (for colors) ----------------------------------------------------------
    // In a repeating pattern each ball follows a fixed cycle ("orbit") through the loop's
    // throws, so a throw's ball can be worked out from its slot alone. Ids therefore don't
    // depend on what part of the ladder is on screen, and scrolling never recolors anything.
    const BallOrbits orbits = computeBallOrbits(loop);
    const int totalBalls = orbits.totalBalls;

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
    if (chain.active) {
        const Slot heldFrom = chain.end == HeldEnd::Arrival ? chain.fixed : chain.hole;
        edit.heldBall = chain.held.value == 0 ? -1 : ballAt(heldFrom);
    }

    // The throws drawn this frame, including throws made above the top edge that are still in
    // the air on screen.
    std::vector<DrawnThrow> drawn;
    for (int b = bFirst - 36; b <= bLast; ++b) {
        for (int j = 0; j < jugglers; ++j) {
            const Slot s{j, b};
            const LoopThrow t = throwAt(s);
            if (t.value <= 0 || b + t.value < bFirst) continue;
            drawn.push_back({s, t, std::max(0, ballAt(s)), throwCurve(s, t)});
        }
    }

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

    if (canvasHovered && (rightClicked || ImGui::IsKeyPressed(ImGuiKey_Escape)) && chain.active)
        cancelLadderEdit(edit);

    // Strip headers: with several jugglers, clicking one selects that juggler (clicking the
    // selected one deselects).
    auto jugglerHeaderRect = [&](int j, ImVec2* min, ImVec2* max) {
        const float cx = stripLeft(j) + stripWidth * 0.5f;
        const float w = ImGui::CalcTextSize("J00").x + fontSize * 0.9f;
        const float top = origin.y + headerHeight + fontSize * 0.15f;
        *min = ImVec2(cx - w * 0.5f, top);
        *max = ImVec2(cx + w * 0.5f, top + fontSize * 1.3f);
    };
    int hoverHeader = -1;
    if (jugglers > 1 && canvasHovered && !chain.active) {
        for (int j = 0; j < jugglers; ++j) {
            ImVec2 a, b;
            jugglerHeaderRect(j, &a, &b);
            if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) hoverHeader = j;
        }
        if (hoverHeader >= 0 && leftClicked)
            result.selectJuggler = hoverHeader == options.selectedJuggler ? -1 : hoverHeader;
    }
    const bool inBody = mouse.y >= bodyTop;

    // Idle hover: which end of which throw would a click pick up?
    int hoverIndex = -1;
    HeldEnd hoverEnd = HeldEnd::Arrival;
    bool hoverZero = false;  // an empty beat (0) under the mouse...
    Slot hoverZeroSlot;      // ...here
    // Holding: which slot would a click drop the held end on?
    bool haveTarget = false;
    Slot target;

    if (canvasHovered && inBody && !chain.active) {
        const HitResult hit = pickThrow([&](const DrawnThrow& d, float t) {
            const HeldEnd e = t < 0.5f ? HeldEnd::Departure : HeldEnd::Arrival;
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
        } else {
            // Empty beats can be picked up too (they "land" on their own beat).
            float bestD = fontSize * 0.6f;
            for (int b = bFirst; b <= bLast; ++b) {
                for (int j = 0; j < jugglers; ++j) {
                    const Slot s{j, b};
                    if (throwAt(s).value != 0) continue;
                    const ImVec2 p = slotPoint(s);
                    const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                    if (d < bestD) {
                        bestD = d;
                        hoverZero = true;
                        hoverZeroSlot = s;
                    }
                }
            }
        }
        edit.hovering = hoverIndex >= 0;
        if (edit.hovering) edit.hoverSlot = drawn[static_cast<size_t>(hoverIndex)].from;
        edit.hoverEnd = hoverEnd;

        if (leftClicked && hoverIndex >= 0) {
            chain = beginEditChain(loop, drawn[static_cast<size_t>(hoverIndex)].from, hoverEnd);
            result.startedChain = true;
        } else if (leftClicked && hoverZero) {
            chain = beginEditChain(loop, hoverZeroSlot, HeldEnd::Arrival);
            result.startedChain = true;
        }
    } else if (canvasHovered && inBody && chain.active) {
        // Near a slot's point, that slot is the target: several curves meet there, and the
        // slot under the mouse is what the user means. Elsewhere, a curve under the mouse
        // targets its relevant end (its landing for a held arrival, its start for a held
        // departure), and failing that the nearest slot within snapping distance.
        const bool arrivalMode = chain.end == HeldEnd::Arrival;
        const float pointPriorityRadius = fontSize * 0.9f;
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

    // ---- Drawing -----------------------------------------------------------------------------
    const float thickness = 2.5f;
    const float arrowSize = fontSize * 0.55f;
    const float markerRadius = fontSize * 0.36f;
    const float dashUnit = fontSize * 0.35f;

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
    if (hoverZero)
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
                dl->AddCircle(p, markerRadius + 5.0f + 2.0f * pulse, mixColor(white, white, 0.0f, 0.95f), 0, 2.0f);
                dl->AddCircle(p, markerRadius + 1.5f, mixColor(white, white, 0.0f, 0.57f), 0, 1.5f);
            } else if (editCloseLoopLength(chain, s, true) > 0) {
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

    // Playhead: where the jugglers are now, at sub-beat resolution, plus the same moment in
    // every other repeat of the loop that's on screen (fainter). Positioned with floats so it
    // glides.
    {
        const float left = areaLeft - fontSize * 0.3f;
        const float right = stripsRight;
        const double localBeat = static_cast<double>(firstBeat);
        const double rel = playheadBeat - localBeat;
        // Copies at playheadBeat + k*period; draw every one that lands on screen. The top-most
        // visible copy is the bright one, so there's always exactly one to follow.
        const double firstCopy = playheadBeat - std::floor(rel / period) * period - period;
        bool primaryDrawn = false;
        for (double b = firstCopy; ; b += period) {
            const float y = y0 + static_cast<float>(b - localBeat) * beatSpacing;
            if (y > maxPt.y + 2.0f) break;
            if (y < bodyTop) continue;
            const bool primary = !primaryDrawn;
            primaryDrawn = true;
            const ImU32 col = primary ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 255, 255, 90);
            if (primary) dl->AddLine(ImVec2(left, y), ImVec2(right, y), IM_COL32(255, 255, 255, 40), 6.0f);
            dl->AddLine(ImVec2(left, y), ImVec2(right, y), col, primary ? 2.0f : 1.0f);
            // A small right-pointing cap, so playhead copies can't be mistaken for beat lines.
            const float cap = fontSize * (primary ? 0.45f : 0.35f);
            dl->AddTriangleFilled(ImVec2(left - cap * 1.1f, y - cap), ImVec2(left - cap * 1.1f, y + cap),
                                  ImVec2(left + cap * 0.2f, y), col);
        }
    }

    // Departure markers on top, ringed in the background color so they stay readable where
    // curves cross them. Empty beats get a small hollow circle.
    for (int b = bFirst; b <= bLast; ++b) {
        for (int j = 0; j < jugglers; ++j) {
            const Slot s{j, b};
            const LoopThrow t = throwAt(s);
            const ImVec2 p0 = slotPoint(s);
            if (t.value == 0) {
                dl->AddCircle(p0, fontSize * 0.25f, dimText, 0, 1.5f);
            } else if (t.value > 0) {
                const BallStyle style = ballStyle(colorVision, std::max(0, ballAt(s)));
                drawMarker(dl, p0, markerRadius, style.shape, style.color, 2.0f, kCanvasBackground);
            }
        }
    }
    if (chain.active) {
        // The held throw's departure marker (its departure reads kNoThrow while held).
        const bool ghost = edit.heldBall < 0;
        const BallStyle heldStyle = ghost ? BallStyle{ghostColor, MarkerShape::Circle, DashPattern::ShortDash}
                                          : ballStyle(colorVision, edit.heldBall);
        Slot from;
        if (chain.end == HeldEnd::Arrival || haveTarget) {
            from = chain.end == HeldEnd::Arrival ? chain.fixed : target;
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
            drawValueLabel(d.curve, throwLabel(d.t, d.from.juggler, jugglers), ballStyle(colorVision, d.ball).color);
        }
    }

    // Tooltip describing what a click would do.
    if (chain.active && haveTarget) {
        LoopThrow t;
        Slot from;
        editDropThrow(chain, target, &t, &from);
        const std::string name = throwLabel(t, from.juggler, jugglers);
        std::string what = "Becomes a " + name;
        if (t.dest != from.juggler) what += " (a pass to J" + std::to_string(t.dest + 1) + ")";
        what += ".";
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
        else
            header = std::to_string(ballCount(pattern)) + " props, period " + std::to_string(period) +
                     ".  Click near either end of a throw to edit it.";
        dl->AddText(ImVec2(origin.x + margin, origin.y + margin * 0.5f),
                    chain.active ? textCol : dimText, header.c_str());
    }

    // Legend: one sample per ball, so identity never depends on color alone.
    {
        float x = origin.x + margin;
        const float y = origin.y + margin * 0.5f + fontSize * 1.9f;
        const float sampleLength = fontSize * 2.6f;
        for (int ball = 0; ball < totalBalls; ++ball) {
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

    // Strip headers: the juggler's number (with several jugglers; the selected one is drawn
    // dark on light, as over their head in the 3D view), then L and R over the columns.
    {
        const float handY = bodyTop - fontSize * 1.25f;
        for (int j = 0; j < jugglers; ++j) {
            if (jugglers > 1) {
                ImVec2 a, b;
                jugglerHeaderRect(j, &a, &b);
                const bool selected = j == options.selectedJuggler;
                const bool hovered = j == hoverHeader;
                const float rounding = fontSize * 0.3f;
                dl->AddRectFilled(a, b, selected ? IM_COL32(236, 238, 244, 255)
                                                 : (hovered ? IM_COL32(52, 56, 68, 255) : IM_COL32(28, 30, 36, 255)),
                                  rounding);
                dl->AddRect(a, b, IM_COL32(210, 214, 224, 230), rounding, 0, selected ? 2.0f : 1.0f);
                const std::string label = "J" + std::to_string(j + 1);
                const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
                            selected ? IM_COL32(16, 16, 20, 255) : IM_COL32(232, 234, 240, 255), label.c_str());
            }
            for (int right = 0; right < 2; ++right) {
                const char* label = right ? "R" : "L";
                const float x = columnX(j, right != 0);
                dl->AddText(ImVec2(x - ImGui::CalcTextSize(label).x * 0.5f, handY), textCol, label);
            }
        }
        if (hoverHeader >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            ImGui::SetTooltip(hoverHeader == options.selectedJuggler ? "Juggler %d (selected): click to deselect"
                                                                     : "Juggler %d: click to select",
                              hoverHeader + 1);
    }

    dl->PopClipRect();
    return result;
}
