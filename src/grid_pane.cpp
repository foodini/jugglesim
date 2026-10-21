// grid_pane.cpp - see grid_pane.h.

#include "grid_pane.h"

#include "app_widgets.h"
#include "color_vision.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {

// The choreography's rows for the siteswap grid (see ChoreoGridView): per juggler, the mark
// they arrive on each beat, loiters, and copies (walk links, rotated repeats) dim.
ChoreoGridView choreoGridView(const Choreography& c, const JugglingLoop& loop, const JuggleParams& params) {
    ChoreoGridView view;
    if (!c.active() || loop.empty()) return view;
    const int written = choreographyCycle(loop, params);
    const int n = loop.jugglers;
    const WalkLoop w = walkLoop(c, n);
    const int full = written * w.phases;
    if (written <= 0 || full <= 0) return view;
    view.active = true;
    view.written = written;
    view.full = full;
    view.loopProblem = w.valid ? std::string() : w.problem;
    for (const SpikeMark& m : c.marks) view.markNames.push_back(m.name);
    view.rows.resize(static_cast<size_t>(n));
    // A juggler's own keyframes in the beats written out (and their loiters).
    auto ownCells = [&](int j) {
        std::vector<ChoreoGridCell> cells(static_cast<size_t>(written));
        if (!c.hasKeys(j)) return cells;
        const std::vector<Keyframe>& keys = c.keys[static_cast<size_t>(j)];
        for (const Keyframe& k : keys) {
            if (k.beat < 0 || k.beat >= written) continue;
            ChoreoGridCell& cell = cells[static_cast<size_t>(k.beat)];
            cell.text = k.mark >= 0 && k.mark < static_cast<int>(c.marks.size())
                            ? c.marks[static_cast<size_t>(k.mark)].name
                            : std::string("?");
            if (k.longTurn) cell.text += "~";
        }
        // Loiters: between two keyframes on the same spot (round the end only when the loop
        // brings them back to their own part, unturned).
        const bool wraps = c.continuation(j).isDefault(j);
        for (size_t i = 0; i < keys.size(); ++i) {
            const bool last = i + 1 == keys.size();
            if (last && !wraps) break;
            const Keyframe& a = keys[i];
            const Keyframe& b = keys[last ? 0 : i + 1];
            if (keys.size() < 2 || !sameKeyframeSpot(c, a, b)) continue;
            const int end = last ? b.beat + written : b.beat;
            for (int beat = a.beat + 1; beat < end; ++beat) cells[static_cast<size_t>(beat % written)].stay = true;
        }
        return cells;
    };
    // Each row over the whole loop: its own path, then (dim) the same again each time round (the
    // rows are positions: who's on one is in its name); or, with a walk link, the leader's,
    // offset (dim; the turn is in the header).
    std::vector<char> state(static_cast<size_t>(n), 0);  // 0 not yet, 1 working on, 2 done
    std::function<const std::vector<ChoreoGridCell>&(int)> rowOf = [&](int j) -> const std::vector<ChoreoGridCell>& {
        ChoreoGridRow& row = view.rows[static_cast<size_t>(j)];
        if (state[static_cast<size_t>(j)] != 0) return row.cells;  // (done, or round in a circle)
        state[static_cast<size_t>(j)] = 1;
        row.cells.assign(static_cast<size_t>(full), ChoreoGridCell());
        const WalkLink l = c.link(j);
        if (l.active() && l.leader >= 0 && l.leader < n && !c.dependsOn(l.leader, j)) {
            const std::vector<ChoreoGridCell> lead = rowOf(l.leader);
            for (int beat = 0; beat < full && !lead.empty(); ++beat) {
                row.cells[static_cast<size_t>(beat)] = lead[static_cast<size_t>(((beat + l.offset) % full + full) % full)];
                row.cells[static_cast<size_t>(beat)].locked = true;
            }
            row.link = "@" + std::to_string(l.leader + 1) + "[" + std::to_string(l.offset) + "]" +
                       (l.turn.none() ? std::string() : " " + turnShort(l.turn));
        } else {
            const std::vector<ChoreoGridCell> part = ownCells(j);
            for (int p = 0; p < w.phases; ++p) {
                for (int beat = 0; beat < written && beat < static_cast<int>(part.size()); ++beat) {
                    ChoreoGridCell cell = part[static_cast<size_t>(beat)];
                    cell.locked = p > 0 || cell.locked;
                    row.cells[static_cast<size_t>(p * written + beat)] = cell;
                }
            }
        }
        state[static_cast<size_t>(j)] = 2;
        return row.cells;
    };
    for (int j = 0; j < n; ++j) {
        rowOf(j);
        ChoreoGridRow& row = view.rows[static_cast<size_t>(j)];
        row.loop = continuationText(c.continuation(j), j);
        row.loopBad = std::find(w.bad.begin(), w.bad.end(), j) != w.bad.end();
    }
    return view;
}

}  // namespace

SiteswapGridResult drawGridPane(GridPane& pane, const DocumentState& doc, const StageState& stage,
                                const Playback& playback, const AppSettings& settings, const JugglingLoop& written,
                                const JugglingLoop& shown, ImVec2 pos, ImVec2 size, ImGuiWindowFlags flags) {
    SiteswapGridResult g;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    pane.siteswapHoverThrow = -1;
    if (ImGui::Begin("Siteswap entry", nullptr, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ThrowLoopGridView throwView;
        throwView.active = written.jugglers > 1;
        {
            const ThrowLoopPlan plan = throwLoopPlan(doc.throwNext, written.jugglers);
            for (int j = 0; j < written.jugglers; ++j) {
                const int k =
                    j < static_cast<int>(doc.throwNext.size()) ? doc.throwNext[static_cast<size_t>(j)] : -1;
                throwView.next.push_back(k >= 0 && k != j ? std::to_string(k + 1) : std::string());
                throwView.bad.push_back(
                    static_cast<char>(std::find(plan.bad.begin(), plan.bad.end(), j) != plan.bad.end()));
            }
            if (stage.derived.played.expanded) throwView.played = stage.derived.played.loop;
            throwView.part = stage.derived.played.part;
            throwView.clash = stage.derived.played.clash;
        }
        ChoreoGridView choreoView = choreoGridView(doc.choreography, shown, doc.juggleParams);
        // Who's on each row at the playhead (the rows are positions; see siteswap_grid.h).
        {
            const int now = static_cast<int>(std::floor(playback.beat));
            throwView.looping = stage.derived.played.part.size() > 1;
            for (int r = 0; r < written.jugglers && throwView.looping; ++r) {
                int who = r;
                for (int j = 0; j < written.jugglers; ++j)
                    if (stage.derived.played.partAt(j, now) == r) who = j;
                throwView.who.push_back(who == r ? -1 : who);
            }
            choreoView.looping = !stage.derived.paths.trivial();
            for (int k = 0; k < static_cast<int>(choreoView.rows.size()) && choreoView.looping; ++k) {
                const int who = stage.derived.paths.walker(k, now);
                choreoView.who.push_back(who == k ? -1 : who);
            }
        }
        g = drawSiteswapGrid(pane.siteswapGrid, doc.siteswapText, doc.parsed, doc.patternCount,
                             settings.colorVision, playback.beat, choreoView, throwView);
        // Resting on a throw describes it and picks it out on the ladder.
        const Siteswap& read = doc.parsed;
        if (g.hoverJuggler >= 0 && stage.derived.played.expanded && g.hoverJuggler < stage.derived.played.loop.jugglers &&
            g.hoverColumn >= 0) {
            // (The loop played out: the throw made then by whoever's on that row.)
            const int beat = g.hoverColumn % stage.derived.played.loop.period;
            int who = g.hoverJuggler;
            for (int j = 0; j < stage.derived.played.loop.jugglers; ++j)
                if (stage.derived.played.partAt(j, beat) == g.hoverJuggler) who = j;
            pane.siteswapHoverThrow = who * stage.derived.played.loop.period + beat;
            std::string says = describeThrow(stage.derived.played.loop, who, beat);
            if (stage.derived.played.part.size() > 1)
                says += "\n(T" + std::to_string(g.hoverJuggler + 1) + "'s throw, made by J" + std::to_string(who + 1) + ")";
            ImGui::SetTooltip("%s", says.c_str());
        } else if (g.hoverJuggler >= 0 && !read.loop.empty() && g.hoverJuggler < read.loop.jugglers) {
            const int beat = g.hoverBeat % read.loop.period;
            if (read.valid || read.sketch)
                pane.siteswapHoverThrow = g.hoverJuggler * read.loop.period + beat;
            ImGui::SetTooltip("%s", describeThrow(read.loop, g.hoverJuggler, beat).c_str());
        }

        // The status line, beside the Count button.
        if (!pane.siteswapBox.note.empty() && ImGui::GetTime() < pane.siteswapBox.noteUntil)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)), "%s",
                               pane.siteswapBox.note.c_str());
        else if (!pane.siteswapBox.info.empty() && ImGui::GetTime() < pane.siteswapBox.infoUntil)
            ImGui::TextUnformatted(pane.siteswapBox.info.c_str());
        else if (stage.derived.problem(DocumentProblemKind::ThrowingLoop))
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                               "The throwing loop doesn't work: %s (the stage shows it as it last worked).",
                               stage.derived.problem(DocumentProblemKind::ThrowingLoop)->text.c_str());
        else if (stage.derived.problem(DocumentProblemKind::WalkingLoop))
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                               "The walking loop doesn't work: %s (the stage shows it as it last worked).",
                               stage.derived.problem(DocumentProblemKind::WalkingLoop)->text.c_str());
        else if (doc.patternValid && doc.pattern.jugglers > 1)
            ImGui::TextDisabled("%d jugglers, %d objects, period %d", doc.pattern.jugglers, ballCount(doc.pattern),
                                loopPeriodBeats(doc.pattern));
        else if (doc.patternValid)
            ImGui::TextDisabled("%d objects, period %d", ballCount(doc.pattern), loopPeriodBeats(doc.pattern));
        else if (doc.sketching)
            ImGui::TextDisabled("Sketch: %d throw%s not decided yet, period %d", doc.parsed.openThrows,
                                doc.parsed.openThrows == 1 ? "" : "s", doc.sketchLoop.period);
        else if (!doc.parsed.error.empty())
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                               "%s", doc.parsed.error.c_str());
        else
            ImGui::TextUnformatted("");
        // Where the cursor is ("J1, beat 5, right hand: 3p+2"), at the right.
        if (!g.readout.empty()) {
            ImGui::SameLine();
            const float width = ImGui::CalcTextSize(g.readout.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
            ImGui::TextUnformatted(g.readout.c_str());
        }
    }
    ImGui::End();
    return g;
}
