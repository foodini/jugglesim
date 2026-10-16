// siteswap_grid.cpp - see siteswap_grid.h.
#include "siteswap_grid.h"

#include "imgui.h"
#include "loop_ops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

namespace {

constexpr float kGap = 2.0f;   // between cells
constexpr float kPadX = 3.0f;  // inside a cell, either side of its text
constexpr int kMaxCount = 8;   // the highest count offered

float cellHeight() { return ImGui::GetTextLineHeight() + 4.0f; }
float beatRowHeight() { return ImGui::GetTextLineHeight() + 2.0f; }

// ---- Typing a throw into a cell

// What's been typed so far, as a throw: a self until the pass is complete (`finished`: a pass
// left without its target stays a pass, so it shows as an error rather than quietly vanishing).
GridCell typedCell(const std::string& typed, int jugglers, bool finished = false) {
    GridCell cell;
    if (typed.empty()) return cell;
    cell.value = throwValue(typed[0]);
    if (typed.size() < 2) return cell;
    if (jugglers == 2) {
        cell.pass = true;  // (the other juggler)
    } else if (typed.size() >= 3 && std::isdigit(static_cast<unsigned char>(typed[2]))) {
        cell.pass = true;
        cell.target = typed[2] - '0';
    } else if (typed.size() >= 4) {
        cell.pass = true;
        cell.relative = true;
        cell.along = (typed[3] - '0') * (typed[2] == '-' ? -1 : 1);
    } else if (finished) {
        cell.pass = true;
    }
    return cell;
}

// ---- Linked rows: what they throw, from the parsed pattern

GridCell derivedCell(const Siteswap& parsed, int juggler, int beat) {
    GridCell cell;
    const JugglingLoop& loop = parsed.loop;
    if (loop.empty() || juggler >= loop.jugglers) return cell;
    const LoopThrow& t = loop.at(juggler, beat);
    if (t.value == kOpenThrow) return cell;
    cell.value = t.value;
    if (t.dest != juggler) {
        cell.pass = true;
        const int n = loop.jugglers;
        if (n > 2) {
            if (parsed.form.relative) {
                cell.relative = true;
                cell.along = ((t.dest - juggler) % n + n) % n;
                if (parsed.form.negative) cell.along -= n;
            } else {
                cell.target = t.dest + 1;
            }
        }
    }
    return cell;
}

}  // namespace

// The gap between the throws' rows and the choreography's.
float sectionGap() { return ImGui::GetTextLineHeight() * 0.6f; }

float siteswapGridHeight(int jugglers, int choreoRows) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return beatRowHeight() + kGap + static_cast<float>(std::max(1, jugglers)) * (cellHeight() + kGap) +
           (choreoRows > 0 ? sectionGap() + static_cast<float>(choreoRows) * (cellHeight() + kGap) : 0.0f) +
           style.ScrollbarSize + style.ItemSpacing.y + ImGui::GetFrameHeight();
}

SiteswapGridResult drawSiteswapGrid(SiteswapGridState& state, const std::string& text, const Siteswap& parsed,
                                    int countN, ColorVisionMode colorVision, double playBeat, const ChoreoGridView& choreo,
                                    const ThrowLoopGridView& throwLoop) {
    SiteswapGridResult res;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushID("siteswapGrid");

    GridCount count;
    count.n = countN >= 2 ? countN : 0;
    SiteswapGrid grid;
    const bool readable = gridFromText(text, &grid);
    if (!readable) {
        // (Siteswap the grid can't hold: sync or multiplex, say.) Shown as it is; pasting over
        // everything (Ctrl+A, Ctrl+V) replaces it.
        grid = SiteswapGrid();
        grid.rows.emplace_back();
    }
    if (grid.period == 0) {
        grid.period = 1;
        for (GridRow& row : grid.rows)
            if (!row.linked()) row.cells.assign(1, GridCell());
    }
    if (text != state.lastText) {
        state.editing = false;  // changed some other way (undo, the ladder, the library)
        state.lastText = text;
    }
    const int rows = grid.jugglers();
    // The choreography's rows (if there's a choreography), in the same columns: there are as
    // many as the longer of the two needs; past its own end, each shows its repeat (dim).
    const bool hasChoreo = choreo.active && choreo.full > 0 && choreo.written > 0 && !choreo.rows.empty();
    const int choreoRows = hasChoreo ? static_cast<int>(choreo.rows.size()) : 0;
    // The throwing loop: a next column after the period (passing patterns), and the loop played
    // out past it.
    const bool hasThrowNext = throwLoop.active && readable && rows > 1;
    const int playedLen =
        !throwLoop.played.empty() && throwLoop.played.jugglers == rows ? throwLoop.played.period : 0;
    constexpr int kMaxColumns = 2048;  // (copies past this aren't shown)
    const int columns = std::min(kMaxColumns, std::max({grid.period, hasChoreo ? choreo.full : 0, playedLen}));
    if (!hasChoreo) state.inChoreo = false;
    if (!hasThrowNext) state.inThrowNext = false;
    // Row names: positions (T2: whose throws; C2: whose path) when there's more than one juggler.
    auto tName = [&](int r) { return std::string(rows > 1 ? "T" : "J") + std::to_string(r + 1); };
    auto cName = [&](int k) { return std::string(choreoRows > 1 ? "C" : "J") + std::to_string(k + 1); };
    // Whose throws juggler r makes in column b (theirs, unless the throwing loop says otherwise).
    auto partAt = [&](int r, int b) {
        if (throwLoop.part.size() <= 1 || grid.period <= 0) return r;
        const int phase = (b / grid.period) % static_cast<int>(throwLoop.part.size());
        const std::vector<int>& parts = throwLoop.part[static_cast<size_t>(phase)];
        return r < static_cast<int>(parts.size()) ? parts[static_cast<size_t>(r)] : r;
    };
    auto clampCursor = [&]() {
        state.row = std::clamp(state.row, 0, rows - 1);
        state.beat = std::clamp(state.beat, 0, grid.period - 1);
        state.anchorRow = std::clamp(state.anchorRow, 0, rows - 1);
        state.anchorBeat = std::clamp(state.anchorBeat, 0, grid.period - 1);
        if (hasChoreo) {
            state.choreoRow = std::clamp(state.choreoRow, 0, choreoRows - 1);
            state.choreoBeat = std::clamp(state.choreoBeat, 0, columns - 1);
        }
    };
    clampCursor();

    // ---- Edits: each changes the grid, and is either shown (while typing) or an undo step.
    std::string madeText = text;
    auto emit = [&](bool commit) {
        madeText = gridToText(grid);
        state.lastText = madeText;
        res.changed = madeText != text;
        res.text = madeText;
        if (commit) {
            res.commit = true;
            res.commitText = madeText;
        }
    };
    auto note = [&](const std::string& why) { res.note = why; };
    auto cellAt = [&](int r, int b) -> GridCell& {
        return grid.rows[static_cast<size_t>(r)].cells[static_cast<size_t>(b)];
    };
    // Where a cell's throw is written: itself, or for a linked row ("@1[3]") the cell it copies.
    // Typing in a copy edits that cell, and every copy of it changes with it.
    auto source = [&](int r, int b, int* sr, int* sb) {
        if (gridCellSource(grid, r, b, sr, sb)) return true;
        *sr = r;
        *sb = b;
        return false;
    };
    auto editable = [&](int r, int b) {
        int sr = r, sb = b;
        if (grid.rows[static_cast<size_t>(r)].linked() && !source(r, b, &sr, &sb)) return false;
        return isEditableCell(grid, sr, sb, count);
    };
    // The cell the cursor's typing goes into.
    auto target = [&]() -> GridCell& {
        int sr = state.row, sb = state.beat;
        source(state.row, state.beat, &sr, &sb);
        return grid.rows[static_cast<size_t>(sr)].cells[static_cast<size_t>(sb)];
    };
    auto snapToCount = [&](int r, int b) {
        // A filler cell (greyed out) stands for its count's beat.
        if (isFillerCell(grid, r, b, count)) b = b / count.n * count.n;
        return b;
    };
    auto commitEdit = [&]() {
        if (!state.editing) return;
        state.editing = false;
        const GridCell finished = typedCell(state.editText, rows, true);
        target() = finished;
        emit(true);
    };
    auto cancelEdit = [&]() {
        if (!state.editing) return;
        state.editing = false;
        target() = state.editOriginal;
        emit(false);
    };
    auto showTyped = [&]() {
        target() = typedCell(state.editText, rows);
        emit(false);
    };
    auto startEdit = [&](char c) {
        if (!editable(state.row, state.beat)) return;
        state.editing = true;
        state.editOriginal = target();
        state.editText = std::string(1, c);
        state.anchorRow = state.row;
        state.anchorBeat = state.beat;
        showTyped();
    };
    auto moveTo = [&](int r, int b, bool extend) {
        state.inThrowNext = false;
        state.row = r;
        state.beat = b;
        if (!extend) {
            state.anchorRow = r;
            state.anchorBeat = b;
        }
        state.scrollToCursor = true;
    };
    // The next editable cell after (r, b): along the row, then (wrap) the next rows'.
    auto nextEditable = [&](int r, int b, bool wrap, int* outR, int* outB) {
        for (int rr = r; rr < rows; ++rr) {
            for (int bb = (rr == r ? b + 1 : 0); bb < grid.period; ++bb)
                if (editable(rr, bb)) {
                    *outR = rr;
                    *outB = bb;
                    return true;
                }
            if (!wrap) break;
        }
        return false;
    };
    auto previousEditable = [&](int r, int b, bool wrap, int* outR, int* outB) {
        for (int rr = r; rr >= 0; --rr) {
            for (int bb = (rr == r ? b - 1 : grid.period - 1); bb >= 0; --bb)
                if (editable(rr, bb)) {
                    *outR = rr;
                    *outB = bb;
                    return true;
                }
            if (!wrap) break;
        }
        return false;
    };
    // After a finished throw: on to the next cell, if there is one.
    auto advance = [&]() {
        int r = 0, b = 0;
        if (nextEditable(state.row, state.beat, true, &r, &b)) {
            moveTo(r, b, false);
            return true;
        }
        return false;
    };
    auto selection = [&](int* r0, int* r1, int* b0, int* b1) {
        *r0 = std::min(state.row, state.anchorRow);
        *r1 = std::max(state.row, state.anchorRow);
        *b0 = std::min(state.beat, state.anchorBeat);
        *b1 = std::max(state.beat, state.anchorBeat);
    };
    auto wholeSelected = [&]() {
        int r0, r1, b0, b1;
        selection(&r0, &r1, &b0, &b1);
        return r0 == 0 && r1 == rows - 1 && b0 == 0 && b1 == grid.period - 1 && (rows > 1 || grid.period > 1);
    };
    auto clearSelection = [&]() {
        int r0, r1, b0, b1;
        selection(&r0, &r1, &b0, &b1);
        for (int r = r0; r <= r1; ++r) {
            for (int b = b0; b <= b1; ++b) {
                int sr = r, sb = b;
                if (grid.rows[static_cast<size_t>(r)].linked() && !source(r, b, &sr, &sb)) continue;
                cellAt(sr, sb) = count.active() && !count.countBeat(sb) ? selfCell(count.filler) : GridCell();
            }
        }
        emit(true);
    };
    // What the grid shows, linked rows written out (for copying).
    auto shownGrid = [&]() {
        SiteswapGrid shown = grid;
        for (int r = 0; r < rows; ++r) {
            GridRow& row = shown.rows[static_cast<size_t>(r)];
            if (!row.linked()) continue;
            row.link.clear();
            row.cells.clear();
            for (int b = 0; b < grid.period; ++b) row.cells.push_back(derivedCell(parsed, r, b));
        }
        return shown;
    };
    auto copy = [&]() {
        if (wholeSelected() || (rows == 1 && grid.period == 1)) {
            ImGui::SetClipboardText(text.c_str());
            return;
        }
        int r0, r1, b0, b1;
        selection(&r0, &r1, &b0, &b1);
        ImGui::SetClipboardText(cellsToText(shownGrid(), r0, r1, b0, b1).c_str());
    };
    // Pasting into cells, once it's settled where: `step` apart, the grid made bigger if asked.
    auto finishPaste = [&](bool extend) {
        const SiteswapGridState::Paste& p = state.paste;
        const int needPeriod = p.beat + (p.clip.period - 1) * p.step + 1;
        const int needRows = p.row + p.clip.jugglers();
        if (extend) {
            int period = needPeriod;
            if (count.active()) period = (period + count.n - 1) / count.n * count.n;
            extendGrid(&grid, period, needRows, count);
        }
        for (int i = 0; i < p.clip.jugglers(); ++i) {
            const int r = p.row + i;
            if (r >= grid.jugglers()) break;
            for (int k = 0; k < p.clip.period; ++k) {
                const int b = p.beat + k * p.step;
                if (b >= grid.period) break;
                cellAt(r, b) = p.clip.rows[static_cast<size_t>(i)].cells[static_cast<size_t>(k)];
            }
        }
        state.paste.active = false;
        emit(true);
    };
    auto continuePaste = [&]() {
        const SiteswapGridState::Paste& p = state.paste;
        const int needPeriod = p.beat + (p.clip.period - 1) * p.step + 1;
        const int needRows = p.row + p.clip.jugglers();
        if (needRows > kMaxJugglers) {
            note("That would need " + std::to_string(needRows) + " jugglers; JuggleSim handles up to " +
                 std::to_string(kMaxJugglers) + ".");
            state.paste.active = false;
            return;
        }
        if (needPeriod > grid.period || needRows > grid.jugglers()) {
            state.askPasteExtend = true;
            return;
        }
        finishPaste(false);
    };
    auto paste = [&]() {
        const char* clipText = ImGui::GetClipboardText();
        SiteswapGrid clip;
        if (!clipText || !gridFromText(clipText, &clip) || (clip.period == 0 && !clip.rows[0].linked())) {
            note("The clipboard doesn't hold siteswap.");
            return;
        }
        if (wholeSelected() || !readable) {
            // Everything selected: the pasted pattern replaces it, links and all.
            grid = clip;
            emit(true);
            res.restart = true;
            if (count.active() && !gridFitsCount(clip, count.n)) res.newCount = 0;
            moveTo(0, 0, false);
            return;
        }
        for (const GridRow& row : clip.rows)
            if (row.linked()) {
                note("A link can't go into cells. To paste a whole pattern, select everything first (Ctrl+A).");
                return;
            }
        for (int i = 0; i < clip.jugglers() && state.row + i < rows; ++i)
            if (grid.rows[static_cast<size_t>(state.row + i)].linked()) {
                note(tName(state.row + i) + " is a copy of another row: unlink it first.");
                return;
            }
        state.paste.active = true;
        state.paste.clip = clip;
        state.paste.row = state.row;
        state.paste.beat = state.beat;
        state.paste.step = 1;
        if (count.active() && count.countBeat(state.beat) && clip.period > 1)
            state.askPasteWhere = true;  // literally, or onto the count's beats?
        else
            continuePaste();
    };
    auto swap = [&]() {
        // The selection's two end cells (in one row), or the cursor's cell and the editable one
        // before it (round the end of the pattern, from the first).
        const int r = state.row;
        if (grid.rows[static_cast<size_t>(r)].linked()) return;
        int first = 0, second = 0, d = 0;
        if (state.anchorRow == state.row && state.anchorBeat != state.beat) {
            first = std::min(state.beat, state.anchorBeat);
            second = std::max(state.beat, state.anchorBeat);
            d = second - first;
        } else if (state.anchorRow != state.row) {
            note("Select two throws in one juggler's row to swap them.");
            return;
        } else {
            second = state.beat;
            int pr = 0, pb = 0;
            if (previousEditable(r, second, false, &pr, &pb)) {
                first = pb;
                d = second - first;
            } else {
                int lr = 0, lb = 0;
                if (!previousEditable(r, grid.period, false, &lr, &lb) || lb == second) {
                    note("Swapping needs two throws.");
                    return;
                }
                first = lb;
                d = second + grid.period - first;
            }
        }
        std::string why;
        SiteswapGrid changed = grid;
        if (!swapCells(&changed, r, first, second, d, &why)) {
            note(why);
            return;
        }
        grid = changed;
        emit(true);
    };

    // ---- The choreography's rows: a mark's name typed into a cell is a keyframe on that beat.
    auto choreoCell = [&](int k, int b) -> const ChoreoGridCell& {
        const std::vector<ChoreoGridCell>& cells = choreo.rows[static_cast<size_t>(k)].cells;
        static const ChoreoGridCell none;
        if (cells.empty()) return none;
        return cells[static_cast<size_t>(((b % static_cast<int>(cells.size())) + static_cast<int>(cells.size())) %
                                          static_cast<int>(cells.size()))];
    };
    // Whether a cell can be edited here; if not, says why.
    auto choreoEditable = [&](int k, int b, bool say) {
        const ChoreoGridRow& row = choreo.rows[static_cast<size_t>(k)];
        if (!row.link.empty()) {
            if (say)
                note(cName(k) + " walks like another row (" + row.link + "): edit the leader's row, or Unlink (right-click " +
                     cName(k) + ").");
            return false;
        }
        if (b >= choreo.written) {
            if (say)
                note("Beat " + std::to_string(b + 1) + " is " +
                     (b < choreo.full ? std::string("the walking loop's copy") : std::string("the choreography coming round again")) +
                     ": only beats 1-" + std::to_string(choreo.written) + " are written out (the next column, beside " + cName(k) +
                     ", says what comes after).");
            return false;
        }
        return true;
    };
    // The mark being typed, as a keyframe in the cursor's cell ("5" alone is "05"). False if
    // there's no such mark (refused, with a plip).
    auto choreoTake = [&]() {
        std::string name = state.choreoTyped;
        state.choreoTyped.clear();
        if (name.empty()) return false;
        if (state.inLoop) {  // the loop column: what was typed, as typed (the app reads it)
            ChoreoEdit e;
            e.kind = ChoreoEdit::Kind::Loop;
            e.juggler = state.choreoRow;
            e.mark = name;
            res.choreoEdits.push_back(e);
            return true;
        }
        if (name.size() == 1) name = "0" + name;
        if (std::find(choreo.markNames.begin(), choreo.markNames.end(), name) == choreo.markNames.end()) {
            note("No spike mark is called " + name + ".");
            res.plip = true;
            return false;
        }
        ChoreoEdit e;
        e.kind = ChoreoEdit::Kind::Set;
        e.juggler = state.choreoRow;
        e.beat = state.choreoBeat;
        e.mark = name;
        res.choreoEdits.push_back(e);
        return true;
    };
    // To a cell (anything half typed is taken first, where it was: "5" then an arrow is "05").
    auto choreoGo = [&](int k, int b) {
        if (!state.choreoTyped.empty() && state.inChoreo) choreoTake();
        state.inThrowNext = false;
        state.inChoreo = true;
        state.inLoop = false;
        state.choreoRow = k;
        state.choreoBeat = b;
        state.choreoTyped.clear();
        state.scrollToCursor = true;
        res.scrubBeat = static_cast<double>(b);
        res.selectJuggler = k;
    };
    // To a juggler's loop cell (anything half typed taken first).
    auto loopGo = [&](int k) {
        if (!state.choreoTyped.empty() && state.inChoreo) choreoTake();
        state.inThrowNext = false;
        state.inChoreo = true;
        state.inLoop = true;
        state.choreoRow = k;
        state.choreoTyped.clear();
        state.scrollToCursor = true;
    };
    // The loop column back to the juggler's own part, unturned.
    auto loopClear = [&]() {
        ChoreoEdit e;
        e.kind = ChoreoEdit::Kind::Loop;
        e.juggler = state.choreoRow;
        res.choreoEdits.push_back(e);
    };
    // The mark typed: a keyframe there, and on to the next beat.
    auto choreoCommit = [&]() {
        if (choreoTake() && state.choreoBeat + 1 < choreo.written) choreoGo(state.choreoRow, state.choreoBeat + 1);
    };
    auto choreoClear = [&]() {
        if (!choreoEditable(state.choreoRow, state.choreoBeat, true)) {
            res.plip = true;
            return;
        }
        ChoreoEdit e;
        e.kind = ChoreoEdit::Kind::Clear;
        e.juggler = state.choreoRow;
        e.beat = state.choreoBeat;
        res.choreoEdits.push_back(e);
    };

    // ---- Layout
    const float cellH = cellHeight();
    const float beatH = beatRowHeight();
    const float wideW = ImGui::CalcTextSize("3p+1").x + 2.0f * kPadX;
    const float narrowW = ImGui::CalcTextSize("3").x + 2.0f * kPadX;
    std::vector<std::string> headers;
    float headerW = 0.0f;
    const float tagW = ImGui::CalcTextSize(" J6").x;  // (room for who's on a row, while a loop's on)
    for (int r = 0; r < rows; ++r) {
        const GridRow& row = grid.rows[static_cast<size_t>(r)];
        std::string h = tName(r);
        if (row.linked()) h += " " + row.link;
        if (row.options.find("LRswap") != std::string::npos) h += " LR";
        headers.push_back(h);
        headerW = std::max(headerW, ImGui::CalcTextSize(h.c_str()).x + 2.0f * kPadX + (throwLoop.looping ? tagW : 0.0f));
    }
    std::vector<std::string> choreoHeaders;
    for (int k = 0; k < choreoRows; ++k) {
        const std::string& link = choreo.rows[static_cast<size_t>(k)].link;
        const std::string h = cName(k) + (link.empty() ? std::string() : " " + link);
        choreoHeaders.push_back(h);
        headerW = std::max(headerW, ImGui::CalcTextSize(h.c_str()).x + 2.0f * kPadX + (choreo.looping ? tagW : 0.0f));
    }
    // Column widths: narrow for a count's filler beats (only ever a 3), else room for "3p+1". With
    // a choreography, every column is wide (a keyframe can be on any beat).
    std::vector<float> colX(static_cast<size_t>(columns) + 1, 0.0f), colW(static_cast<size_t>(columns), wideW);
    // The next column (beside the row names, not scrolling): whose throws or path each row goes on
    // to after the period or the walking's length. A bar after each one's last written beat marks
    // where its copies start.
    const bool hasNextColumn = hasThrowNext || hasChoreo;
    const float loopW = hasNextColumn ? ImGui::CalcTextSize("4r+1/12").x + 2.0f * kPadX : 0.0f;
    const int loopAt = hasChoreo ? std::clamp(choreo.written, 1, columns) : -1;
    const int throwAt = std::clamp(grid.period, 1, columns);
    auto layoutColumns = [&]() {
        colX.assign(static_cast<size_t>(columns) + 1, 0.0f);
        colW.assign(static_cast<size_t>(columns), wideW);
        for (int b = 0; b < columns; ++b) {
            bool narrow = !hasChoreo && b < grid.period && count.active() && !count.countBeat(b) &&
                          !(state.editing && state.beat == b);
            for (int r = 0; r < rows && narrow; ++r)
                if (!grid.rows[static_cast<size_t>(r)].linked() && !isFillerCell(grid, r, b, count)) narrow = false;
            colW[static_cast<size_t>(b)] = narrow ? narrowW : wideW;
            colX[static_cast<size_t>(b) + 1] = colX[static_cast<size_t>(b)] + colW[static_cast<size_t>(b)] + kGap;
        }
    };
    layoutColumns();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float rowsTop = beatH + kGap;
    auto rowY = [&](int r) { return rowsTop + static_cast<float>(r) * (cellH + kGap); };
    const float choreoTop = rowY(rows) + sectionGap();
    auto choreoRowY = [&](int k) { return choreoTop + static_cast<float>(k) * (cellH + kGap); };
    const float gridH = hasChoreo ? choreoRowY(choreoRows) : rowY(rows);
    bool hoveredAny = false;

    // ---- Row headers (J1, J2 @1[1], ...): click selects the row; right-click, its menu.
    for (int r = 0; r < rows; ++r) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowY(r)));
        ImGui::PushID(r);
        ImGui::InvisibleButton("##header", ImVec2(std::max(1.0f, headerW), cellH),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        ImGui::PopID();
        const bool hovered = ImGui::IsItemHovered();
        hoveredAny = hoveredAny || hovered;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            commitEdit();
            state.focused = true;
            state.inChoreo = false;
            state.inThrowNext = false;
            state.anchorRow = state.row = r;
            state.anchorBeat = 0;
            state.beat = grid.period - 1;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            commitEdit();
            state.focused = true;
            state.menuRow = r;
            ImGui::OpenPopup("##rowMenu");
        }
        if (hovered && r < static_cast<int>(parsed.form.links.size()) && parsed.form.linked(r)) {
            const PartLink& l = parsed.form.links[static_cast<size_t>(r)];
            ImGui::SetTooltip("%s: %s%s.\nRight-click for this row's menu.", tName(r).c_str(),
                              linkWords(l.to, l.start, parsed.loop.period).c_str(), l.lrSwap ? ", hands swapped" : "");
        }
    }
    // ...and the choreography's (J1, J2 @1[16] -60°): click goes to the row; right-click, its menu.
    for (int k = 0; k < choreoRows; ++k) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + choreoRowY(k)));
        ImGui::PushID(1000 + k);
        ImGui::InvisibleButton("##choreoHeader", ImVec2(std::max(1.0f, headerW), cellH),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        ImGui::PopID();
        const bool hovered = ImGui::IsItemHovered();
        hoveredAny = hoveredAny || hovered;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            commitEdit();
            state.focused = true;
            choreoGo(k, state.inChoreo ? state.choreoBeat : 0);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            commitEdit();
            state.focused = true;
            state.choreoMenuRow = k;
            state.choreoMenuBeat = -1;
            ImGui::OpenPopup("##choreoRowMenu");
        }
        if (hovered)
            ImGui::SetTooltip("%s: a walking path, the spike mark it arrives on, beat by beat.\n"
                              "Right-click for Walk Like... and Unlink.", cName(k).c_str());
    }

    // ---- The cells (scrolling sideways when they don't fit).
    const float nextX = origin.x + headerW + kGap;  // (the next column)
    const ImVec2 cellsTopLeft(nextX + (hasNextColumn ? loopW + kGap * 3.0f : 0.0f), origin.y);
    ImGui::SetCursorScreenPos(cellsTopLeft);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float childW = std::max(1.0f, rightEdge - cellsTopLeft.x);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##cells", ImVec2(childW, gridH + style.ScrollbarSize), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    const ImVec2 cellsOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##cellsArea", ImVec2(std::max(1.0f, colX.back()), gridH),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool areaHovered = ImGui::IsItemHovered();
    hoveredAny = hoveredAny || ImGui::IsWindowHovered();
    // The wheel scrolls sideways.
    if (ImGui::IsWindowHovered() && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
        ImGui::SetScrollX(ImGui::GetScrollX() - (io.MouseWheel + io.MouseWheelH) * wideW * 2.0f);
    // The cell under the mouse: beat -1 for none; row -1 for the beat numbers, 0... a juggler's
    // throws, or kChoreoRow with *k the choreography row; -2 for the gap between.
    constexpr int kChoreoRow = -3;
    bool openChoreoMenu = false, openThrowNextMenu = false;
    int hoverRow = -2, hoverBeat = -1, hoverChoreo = -1;
    auto cellUnder = [&](ImVec2 at, int* r, int* b, int* k) {
        const float x = at.x - cellsOrigin.x, y = at.y - cellsOrigin.y;
        *b = -1;
        *k = -1;
        for (int c = 0; c < columns; ++c)
            if (x >= colX[static_cast<size_t>(c)] && x < colX[static_cast<size_t>(c)] + colW[static_cast<size_t>(c)] + kGap) *b = c;
        if (y < rowsTop) {
            *r = -1;
        } else if (!hasChoreo || y < rowY(rows)) {
            *r = std::min(rows - 1, static_cast<int>((y - rowsTop) / (cellH + kGap)));
        } else if (y < choreoTop) {
            *r = -2;
        } else {
            *r = kChoreoRow;
            *k = std::min(choreoRows - 1, static_cast<int>((y - choreoTop) / (cellH + kGap)));
        }
    };
    if (areaHovered) cellUnder(io.MousePos, &hoverRow, &hoverBeat, &hoverChoreo);
    // A throw's column past the pattern's end shows its repeat: that's the throw it is.
    auto throwBeat = [&](int b) { return grid.period > 0 ? b % grid.period : 0; };

    // Mouse: click a cell (Shift+click: select to it), drag to select, Ctrl+click: make the
    // cursor's juggler a copy of the clicked one, from that beat. Click a beat number: its column.
    // In the choreography's rows: click a cell to go to its beat (paused, that juggler selected);
    // Ctrl+click makes the cursor's juggler walk like the clicked one, from that beat.
    // The loop column: click a juggler's cell to type there; right-click for its builder.
    if (areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverBeat >= 0 && hoverRow != -2) {
        commitEdit();
        state.focused = true;
        if (hoverRow == kChoreoRow) {
            if (io.KeyCtrl && state.inChoreo) {
                if (hoverChoreo == state.choreoRow) {
                    note("Ctrl+click a beat in another juggler's row: J" + std::to_string(state.choreoRow + 1) +
                         " walks like them, from that beat on.");
                } else {
                    res.walkLinkJuggler = state.choreoRow;
                    res.walkLinkLeader = hoverChoreo;
                    res.walkLinkOffset = hoverBeat;
                }
            } else {
                choreoGo(hoverChoreo, hoverBeat);
            }
        } else if (hoverRow == -1) {
            state.inChoreo = false;
            state.anchorRow = 0;
            state.row = rows - 1;
            state.anchorBeat = state.beat = throwBeat(hoverBeat);
        } else if (io.KeyCtrl) {
            state.inChoreo = false;
            if (hoverRow == state.row) {
                note("Ctrl+click a throw in another juggler's row: J" + std::to_string(state.row + 1) +
                     " becomes a copy of them, from that throw on.");
            } else if (rows > 1) {
                res.linkJuggler = state.row;
                res.linkTo = hoverRow;
                res.linkStart = throwBeat(hoverBeat);
            }
        } else {
            state.inChoreo = false;
            // (A copy past the period: the throw it is, in the part being thrown then.)
            moveTo(hoverRow, snapToCount(hoverRow, throwBeat(hoverBeat)), io.KeyShift);
            if (hoverBeat >= grid.period) state.scrollToCursor = false;  // (a repeat: its throw, where it is)
            state.selecting = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) state.selecting = false;
    if (state.selecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        int r = 0, b = -1, k = -1;
        cellUnder(io.MousePos, &r, &b, &k);
        if (b < 0) b = io.MousePos.x < cellsOrigin.x ? 0 : grid.period - 1;
        r = std::clamp(r, 0, rows - 1);
        state.row = r;
        state.beat = std::min(b, grid.period - 1);
    }
    if (areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hoverBeat >= 0 && hoverRow != -2) {
        commitEdit();
        state.focused = true;
        if (hoverRow == kChoreoRow) {
            choreoGo(hoverChoreo, hoverBeat);
            state.choreoMenuRow = hoverChoreo;
            state.choreoMenuBeat = hoverBeat;
            openChoreoMenu = true;  // (opened outside the scrolling cells, where its menu is)
        } else {
            state.inChoreo = false;
            const int r = std::max(0, hoverRow);
            const int tb = throwBeat(hoverBeat);
            int r0, r1, b0, b1;
            selection(&r0, &r1, &b0, &b1);
            if (r < r0 || r > r1 || tb < b0 || tb > b1) moveTo(r, snapToCount(r, tb), false);
            state.menuRow = r;
            state.menuBeat = tb;
            ImGui::OpenPopup("##cellMenu");
        }
    }
    if (hoverRow >= 0 && hoverBeat >= 0) {
        res.hoverJuggler = hoverRow;
        res.hoverBeat = throwBeat(hoverBeat);
        res.hoverColumn = hoverBeat;
    }

    // ---- Keys (while the grid has the keyboard, and no menu or dialog is up).
    const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    // In the choreography's rows: a mark's name (two characters, or one and Tab: "5" Tab is "05"),
    // ~ to turn the long way round, Delete or Backspace to clear, arrows, Tab, Home and End.
    // A step along a walking row: into the loop column after the beats written out, and out of it.
    // (The next column is before beat 1, beside the row names.)
    auto choreoStep = [&](int dir) {
        if (state.inLoop) {
            if (dir > 0) choreoGo(state.choreoRow, 0);
            return;
        }
        if (dir < 0 && state.choreoBeat == 0) {
            loopGo(state.choreoRow);
            return;
        }
        const int b = state.choreoBeat + dir;
        if (b >= 0 && b < columns) choreoGo(state.choreoRow, b);
    };
    // In the loop column: whose part the juggler takes up next, and how far turned: "2", "2r+1/4",
    // "r1/2" (their own, turned); Enter, Tab or a move takes it; Backspace on nothing, or Delete,
    // puts back their own part, unturned.
    if (state.focused && !popupOpen && state.inChoreo && state.inLoop && hasChoreo) {
        const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
        auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
        if (!ctrl) {
            for (const ImWchar wc : io.InputQueueCharacters) {
                if (wc >= 128) continue;
                const char c = static_cast<char>(std::tolower(static_cast<int>(wc)));
                if ((c >= '0' && c <= '9') || c == 'r' || c == '+' || c == '-' || c == '/') state.choreoTyped += c;
            }
        }
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) choreoTake();
        if (pressed(ImGuiKey_Tab)) {
            choreoTake();
            choreoStep(shift ? -1 : 1);
        }
        if (pressed(ImGuiKey_Escape)) {
            if (!state.choreoTyped.empty())
                state.choreoTyped.clear();
            else
                state.focused = false;
        }
        if (pressed(ImGuiKey_Backspace)) {
            if (!state.choreoTyped.empty())
                state.choreoTyped.pop_back();
            else
                loopClear();
        }
        if (pressed(ImGuiKey_Delete)) {
            state.choreoTyped.clear();
            loopClear();
        }
        if (!ctrl && (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_RightArrow)))
            choreoStep(ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ? 1 : -1);
        if (!ctrl && (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow))) {
            const int k = state.choreoRow + (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ? -1 : 1);
            if (k >= 0 && k < choreoRows) loopGo(k);
        }
    } else if (state.focused && !popupOpen && state.inChoreo && hasChoreo) {
        const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
        auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
        if (!ctrl) {
            for (const ImWchar wc : io.InputQueueCharacters) {
                if (wc >= 128) continue;
                const char c = static_cast<char>(std::tolower(static_cast<int>(wc)));
                if (c == ' ') continue;  // (Space plays and pauses)
                if (c == '~') {
                    if (!choreoEditable(state.choreoRow, state.choreoBeat, true)) {
                        res.plip = true;
                    } else if (choreoCell(state.choreoRow, state.choreoBeat).text.empty()) {
                        note("~ turns the long way round on the way to a keyframe: there isn't one on this beat.");
                        res.plip = true;
                    } else {
                        ChoreoEdit e;
                        e.kind = ChoreoEdit::Kind::LongTurn;
                        e.juggler = state.choreoRow;
                        e.beat = state.choreoBeat;
                        res.choreoEdits.push_back(e);
                    }
                    continue;
                }
                const bool nameChar = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z');
                if (!nameChar) continue;
                if (!choreoEditable(state.choreoRow, state.choreoBeat, true)) {
                    res.plip = true;
                    continue;
                }
                state.choreoTyped += c;
                if (state.choreoTyped.size() >= 2) choreoCommit();
            }
        }
        const int lastWritten = std::max(0, choreo.written - 1);
        if (pressed(ImGuiKey_Tab)) {
            if (!state.choreoTyped.empty()) {
                choreoCommit();
            } else {
                choreoStep(shift ? -1 : 1);
            }
        }
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) choreoCommit();
        if (pressed(ImGuiKey_Escape)) {
            if (!state.choreoTyped.empty())
                state.choreoTyped.clear();
            else
                state.focused = false;
        }
        if (pressed(ImGuiKey_Backspace)) {
            if (!state.choreoTyped.empty())
                state.choreoTyped.pop_back();
            else
                choreoClear();
        }
        if (pressed(ImGuiKey_Delete)) {
            state.choreoTyped.clear();
            choreoClear();
        }
        if (!ctrl && (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_RightArrow))) {
            // (A mark half typed is taken where it is first.)
            if (!state.choreoTyped.empty()) choreoTake();
            choreoStep(ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ? 1 : -1);
        }
        if (!ctrl && (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow))) {
            const int k = state.choreoRow + (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ? -1 : 1);
            if (k >= 0 && k < choreoRows) {
                choreoGo(k, state.choreoBeat);
            } else if (k < 0) {  // up into the throws
                if (!state.choreoTyped.empty()) choreoTake();
                state.inChoreo = false;
                moveTo(rows - 1, snapToCount(rows - 1, throwBeat(state.choreoBeat)), false);
            }
        }
        if (pressed(ImGuiKey_Home)) choreoGo(state.choreoRow, 0);
        if (pressed(ImGuiKey_End)) choreoGo(state.choreoRow, lastWritten);
    } else if (state.focused && !popupOpen && state.inThrowNext) {
        // The throws' next column: a juggler's number (whose throws they take up next), Backspace
        // or Delete for their own; arrows, Tab and Enter move.
        auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
        auto setNext = [&](int part) { res.throwNextEdits.push_back({state.row, part == state.row ? -1 : part}); };
        if (!io.KeyCtrl) {
            for (const ImWchar wc : io.InputQueueCharacters) {
                if (wc >= '1' && wc <= '9') {
                    const int k = static_cast<int>(wc - '1');
                    if (k < rows) {
                        setNext(k);
                    } else {
                        note("There's no T" + std::to_string(k + 1) + ": a row's number, 1-" + std::to_string(rows) + ".");
                        res.plip = true;
                    }
                } else if (wc != ' ' && wc < 128) {
                    note("The next column takes a row's number: which T row whoever's on " + tName(state.row) +
                         " goes on to after beat " + std::to_string(grid.period) + ".");
                    res.plip = true;
                }
            }
        }
        if (pressed(ImGuiKey_Backspace) || pressed(ImGuiKey_Delete)) setNext(-1);
        if (pressed(ImGuiKey_Escape)) state.focused = false;
        if (pressed(ImGuiKey_UpArrow)) state.row = std::max(0, state.row - 1);
        if (pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) ||
            (pressed(ImGuiKey_Tab) && !io.KeyShift))
            state.row = std::min(rows - 1, state.row + 1);
        if (pressed(ImGuiKey_Tab) && io.KeyShift) state.row = std::max(0, state.row - 1);
        if (pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_End)) {
            int r = 0, b = 0;
            const bool end = ImGui::IsKeyPressed(ImGuiKey_End, true);
            if (end ? previousEditable(state.row, grid.period, false, &r, &b) : nextEditable(state.row, -1, false, &r, &b))
                moveTo(r, b, false);
        }
        state.scrollToCursor = state.scrollToCursor || pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow);
    } else if (state.focused && !popupOpen) {
        const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
        // Typed characters: throws, "p", and a target's "+" or "-".
        if (!ctrl) {
            for (const ImWchar wc : io.InputQueueCharacters) {
                if (wc >= 128) continue;
                const char c = static_cast<char>(std::tolower(static_cast<int>(wc)));
                if (c == ' ') continue;  // (Space plays and pauses)
                const bool isThrow = throwValue(c) >= 0;
                if (!state.editing) {
                    if (isThrow) startEdit(c);
                    continue;
                }
                const std::string& typed = state.editText;
                if (typed.empty()) {
                    if (isThrow) {
                        state.editText = std::string(1, c);
                        showTyped();
                    }
                } else if (typed.size() == 1) {
                    if (c == 'p') {
                        if (rows < 2) {
                            note("A solo pattern has no one to pass to (right-click J1 to add a juggler).");
                            continue;
                        }
                        state.editText += 'p';
                        showTyped();
                        if (rows == 2) {
                            commitEdit();
                            advance();
                        }
                    } else if (isThrow) {
                        commitEdit();
                        if (advance())
                            startEdit(c);
                        else
                            note("That's the end of the pattern (right-click a cell to insert columns).");
                    }
                } else if (typed.back() == 'p') {
                    if (c >= '1' && c <= '9' && grid.rows[static_cast<size_t>(state.row)].linked()) {
                        // A copy keeps a target as written: "3p4" here would send every copy of
                        // this throw to J4. Refused; the relative one that does it is suggested.
                        const int to = c - '0', from = state.row + 1;
                        int sr = 0, sb = 0;
                        source(state.row, state.beat, &sr, &sb);
                        std::string why = tName(from - 1) + " copies " + tName(sr) + ", so a pass by number would go to C" +
                                          std::to_string(to) + " from every copy: type it as + or - along";
                        if (to >= 1 && to <= rows && to != from)
                            why += " (" + typed + (to > from ? "+" : "-") + std::to_string(std::abs(to - from)) + " here)";
                        note(why + ".");
                        res.plip = true;
                    } else if (c >= '1' && c <= '9' && c - '0' <= rows) {
                        state.editText += c;
                        commitEdit();
                        advance();
                    } else if (c == '+' || c == '-') {
                        state.editText += c;
                        showTyped();
                    }
                } else if (typed.back() == '+' || typed.back() == '-') {
                    if (c >= '0' && c <= '9') {
                        state.editText += c;
                        commitEdit();
                        advance();
                    }
                }
            }
        }
        auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
        if (pressed(ImGuiKey_Escape)) {
            if (state.editing)
                cancelEdit();
            else
                state.focused = false;
        }
        if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) commitEdit();
        if (pressed(ImGuiKey_Backspace)) {
            if (state.editing) {
                if (!state.editText.empty()) state.editText.pop_back();
                showTyped();
            } else {
                clearSelection();
            }
        }
        if (pressed(ImGuiKey_Delete)) {
            if (state.editing) {
                state.editText.clear();
                commitEdit();
            } else {
                clearSelection();
            }
        }
        // Moving: arrows (Shift: select), Tab / Shift+Tab, Home / End.
        if (!ctrl && (pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_RightArrow))) {
            commitEdit();
            const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);
            int r = 0, b = 0;
            if (right ? nextEditable(state.row, state.beat, false, &r, &b)
                      : previousEditable(state.row, state.beat, false, &r, &b)) {
                moveTo(r, b, shift);
            } else if (!right && !shift && hasThrowNext) {
                state.inThrowNext = true;  // back into the next column, before beat 1
                state.anchorRow = state.row;
                state.anchorBeat = state.beat;
                state.scrollToCursor = true;
            }
        }
        if (pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow)) {
            const int delta = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ? 1 : -1;
            commitEdit();
            if (ctrl && editable(state.row, state.beat)) {
                // Ctrl+Up/Down: the throw one higher or lower; Ctrl+Shift: where it goes (in a
                // copy, of the throw it copies).
                int sr = state.row, sb = state.beat;
                source(state.row, state.beat, &sr, &sb);
                GridCell changed = cellAt(sr, sb);
                std::string why;
                const bool ok = shift ? stepCellTarget(grid, sr, sb, delta, &changed, &why) : bumpCell(&changed, delta);
                if (ok) {
                    cellAt(sr, sb) = changed;
                    emit(true);
                } else if (!why.empty()) {
                    note(why);
                }
            } else if (!ctrl && hasChoreo && !shift && delta < 0 && state.row == rows - 1) {
                choreoGo(0, state.beat);  // down into the choreography's rows
            } else if (!ctrl) {
                const int r = std::clamp(state.row - delta, 0, rows - 1);
                moveTo(r, snapToCount(r, state.beat), shift);
            }
        }
        if (pressed(ImGuiKey_Tab)) {
            commitEdit();
            int r = 0, b = 0;
            if (shift ? previousEditable(state.row, state.beat, true, &r, &b)
                      : nextEditable(state.row, state.beat, true, &r, &b))
                moveTo(r, b, false);
        }
        if (pressed(ImGuiKey_Home) || pressed(ImGuiKey_End)) {
            const bool end = ImGui::IsKeyPressed(ImGuiKey_End, true);
            commitEdit();
            int r = 0, b = 0;
            if (ctrl) {
                if (end ? previousEditable(rows - 1, grid.period, true, &r, &b) : nextEditable(0, -1, true, &r, &b))
                    moveTo(r, b, shift);
            } else if (end ? previousEditable(state.row, grid.period, false, &r, &b)
                           : nextEditable(state.row, -1, false, &r, &b)) {
                moveTo(r, b, shift);
            }
        }
        // Ctrl+A select all, C copy, X cut, V paste, T swap, R / L rotate.
        if (ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
                commitEdit();
                state.anchorRow = 0;
                state.anchorBeat = 0;
                state.row = rows - 1;
                state.beat = grid.period - 1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                commitEdit();
                copy();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                commitEdit();
                copy();
                clearSelection();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                commitEdit();
                paste();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
                commitEdit();
                swap();
            }
            if (!shift && (ImGui::IsKeyPressed(ImGuiKey_R) || ImGui::IsKeyPressed(ImGuiKey_L))) {
                commitEdit();
                const int steps = (ImGui::IsKeyPressed(ImGuiKey_R) ? 1 : -1) * (count.active() ? count.n : 1);
                if (rotateGrid(&grid, steps))
                    emit(true);
                else
                    note("Nothing to rotate: a pattern needs two or more beats.");
            }
        }
    }
    clampCursor();
    layoutColumns();  // (the edits may have changed the columns)

    // Keep the cursor's cell in view.
    // (The next column doesn't scroll.)
    if (state.scrollToCursor && ((state.inChoreo && state.inLoop) || state.inThrowNext)) state.scrollToCursor = false;
    if (state.scrollToCursor) {
        state.scrollToCursor = false;
        const int col = state.inChoreo ? state.choreoBeat : state.beat;
        const float x0 = colX[static_cast<size_t>(col)];
        const float x1 = x0 + colW[static_cast<size_t>(col)];
        const float scroll = ImGui::GetScrollX(), visible = ImGui::GetWindowWidth();
        if (x0 < scroll)
            ImGui::SetScrollX(x0);
        else if (x1 > scroll + visible)
            ImGui::SetScrollX(x1 - visible);
    }

    // ---- Drawing the cells.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 frameBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 linkBg = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.45f);
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 blankBg = blankCellColor(colorVision);
    const ImU32 errorBg = errorCellColor(colorVision);
    const ImU32 selectBg = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    // Problem throws, by the cell they're written in: a copy's problem shows on the original and
    // on every copy of it.
    std::set<std::pair<int, int>> problems;
    for (const Siteswap::ThrowRef& p : parsed.problemThrows) {
        if (grid.period <= 0 || p.juggler < 0 || p.juggler >= rows) continue;
        int sr = 0, sb = 0;
        source(p.juggler, ((p.beat % grid.period) + grid.period) % grid.period, &sr, &sb);
        problems.insert({sr, sb});
    }
    // The cursor's throw, wherever else it shows (its original and its copies): outlined.
    int cursorSourceRow = -1, cursorSourceBeat = -1;
    source(state.row, state.beat, &cursorSourceRow, &cursorSourceBeat);
    // Throws of the loop played out that don't work (two landing together; a hand with nothing).
    std::set<std::pair<int, int>> clashes;
    for (const std::pair<int, int>& c : throwLoop.clash) clashes.insert({partAt(c.first, c.second), c.second});
    const bool throwCursor = !state.inChoreo && !state.inThrowNext;
    const ImU32 twinCol = IM_COL32(120, 205, 255, 255);
    const int nowBeat = columns > 0 ? static_cast<int>(std::floor(playBeat)) % columns : 0;
    int r0, r1, b0, b1;
    selection(&r0, &r1, &b0, &b1);
    const bool rangeSelected = r0 != r1 || b0 != b1;
    for (int b = 0; b < columns; ++b) {
        const float x = cellsOrigin.x + colX[static_cast<size_t>(b)];
        const float w = colW[static_cast<size_t>(b)];
        // The beat number (count beats only, in a count), and a mark under the beat playing now.
        if (colW[static_cast<size_t>(b)] == wideW) {
            const std::string number = std::to_string(b + 1);
            const ImVec2 size = ImGui::CalcTextSize(number.c_str());
            draw->AddText(ImVec2(x + (w - size.x) * 0.5f, cellsOrigin.y), b == nowBeat ? textCol : dimText,
                          number.c_str());
        }
        if (b == nowBeat)
            draw->AddRectFilled(ImVec2(x, cellsOrigin.y + beatH - 1.0f), ImVec2(x + w, cellsOrigin.y + beatH + 1.0f),
                                ImGui::GetColorU32(ImGuiCol_Text, 0.85f));
        // Past the pattern's period (a longer choreography under it): the pattern's repeat, dim.
        const bool repeat = b >= grid.period;
        const int tb = throwBeat(b);
        for (int r = 0; r < rows; ++r) {
            const GridRow& row = grid.rows[static_cast<size_t>(r)];
            const ImVec2 a(x, cellsOrigin.y + rowY(r)), z(x + w, cellsOrigin.y + rowY(r) + cellH);
            const bool linked = row.linked();
            int sr = r, sb = tb;
            const bool sourced = source(r, tb, &sr, &sb);
            // A copy shows the throw it copies, as written there (a copy keeps its form). (Past the
            // period, the row's throws again: the rows are positions; who's on one is in its name.)
            const GridCell cell = !linked ? row.cells[static_cast<size_t>(tb)]
                                          : (sourced ? cellAt(sr, sb) : derivedCell(parsed, r, tb));
            const bool clash = playedLen > 0 && clashes.count({r, b % playedLen}) > 0;
            const bool filler = isFillerCell(grid, sr, sb, count);
            const bool offCount = !linked && count.active() && !count.countBeat(tb) && !filler;
            const bool cursorHere = !repeat && throwCursor && r == state.row && tb == state.beat;
            const bool editingHere = state.editing && cursorHere;
            const bool error = !cell.blank() && (problems.count({sr, sb}) > 0 || offCount || clash);
            const bool twin = !repeat && sourced && sr == cursorSourceRow && sb == cursorSourceBeat && !cursorHere &&
                              state.focused && throwCursor && !rangeSelected;
            ImU32 bg = linked || repeat ? linkBg : frameBg;
            ImU32 fg = filler || linked || repeat ? dimText : textCol;
            if (editingHere) {
                bg = frameBg;
                fg = textCol;
            } else if (cell.blank()) {
                bg = blankBg;
            } else if (error && (!repeat || clash)) {
                bg = errorBg;
                fg = IM_COL32(0, 0, 0, 255);
            }
            draw->AddRectFilled(a, z, bg);
            if (!repeat && throwCursor && rangeSelected && r >= r0 && r <= r1 && tb >= b0 && tb <= b1) draw->AddRectFilled(a, z, selectBg);
            const std::string shown = editingHere ? state.editText : cell.text();
            if (!shown.empty()) {
                const ImVec2 size = ImGui::CalcTextSize(shown.c_str());
                draw->AddText(ImVec2(a.x + (w - size.x) * 0.5f, a.y + (cellH - size.y) * 0.5f), fg, shown.c_str());
                if (editingHere && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
                    const float cx = a.x + (w + size.x) * 0.5f + 1.0f;
                    draw->AddLine(ImVec2(cx, a.y + 2.0f), ImVec2(cx, z.y - 2.0f), textCol, 1.0f);
                }
            } else if (editingHere && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
                const float cx = a.x + w * 0.5f;
                draw->AddLine(ImVec2(cx, a.y + 2.0f), ImVec2(cx, z.y - 2.0f), textCol, 1.0f);
            }
            // The same throw as the cursor's (its original or another copy): outlined, with a
            // bar under it, so it reads without the color.
            if (twin) {
                draw->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(z.x + 1.0f, z.y + 1.0f), twinCol, 0.0f, 0, 1.5f);
                draw->AddRectFilled(ImVec2(a.x + 2.0f, z.y - 3.0f), ImVec2(z.x - 2.0f, z.y - 1.0f), twinCol);
            }
            if (cursorHere)
                draw->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(z.x + 1.0f, z.y + 1.0f),
                              state.focused ? textCol : dimText, 0.0f, 0, state.focused ? 2.0f : 1.0f);
        }
        // The choreography's rows: the mark each juggler arrives on (pale yellow, like the marks
        // on the floor), a bar through a loiter, copies dim.
        for (int k = 0; k < choreoRows; ++k) {
            const ImVec2 a(x, cellsOrigin.y + choreoRowY(k)), z(x + w, cellsOrigin.y + choreoRowY(k) + cellH);
            const ChoreoGridCell& cell = choreoCell(k, b);
            const bool copy = cell.locked || b >= choreo.written;
            const bool cursorHere = state.inChoreo && !state.inLoop && k == state.choreoRow && b == state.choreoBeat;
            draw->AddRectFilled(a, z, copy ? linkBg : frameBg);
            const ImU32 markCol = copy ? IM_COL32(236, 226, 150, 120) : IM_COL32(236, 226, 150, 255);
            if (cell.stay)
                draw->AddRectFilled(ImVec2(a.x - kGap, (a.y + z.y) * 0.5f - 1.5f), ImVec2(z.x, (a.y + z.y) * 0.5f + 1.5f), markCol);
            const std::string shown = cursorHere && !state.choreoTyped.empty() ? state.choreoTyped : cell.text;
            if (!shown.empty()) {
                const ImVec2 size = ImGui::CalcTextSize(shown.c_str());
                draw->AddText(ImVec2(a.x + (w - size.x) * 0.5f, a.y + (cellH - size.y) * 0.5f),
                              cursorHere && !state.choreoTyped.empty() ? textCol : markCol, shown.c_str());
            }
            if (cursorHere && !state.choreoTyped.empty() && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
                const float cx = a.x + (w + ImGui::CalcTextSize(shown.c_str()).x) * 0.5f + 1.0f;
                draw->AddLine(ImVec2(cx, a.y + 2.0f), ImVec2(cx, z.y - 2.0f), textCol, 1.0f);
            }
            if (cursorHere)
                draw->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(z.x + 1.0f, z.y + 1.0f),
                              state.focused ? textCol : dimText, 0.0f, 0, state.focused ? 2.0f : 1.0f);
        }
    }

    // Bars after the throws' period and the walking's length: where their copies start.
    {
        const ImU32 barCol = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
        if (throwAt < columns) {
            const float x = cellsOrigin.x + colX[static_cast<size_t>(throwAt)] - kGap * 0.5f;
            draw->AddRectFilled(ImVec2(x - 1.5f, cellsOrigin.y + rowY(0) - 1.0f), ImVec2(x + 1.5f, cellsOrigin.y + rowY(rows) - kGap + 1.0f),
                                barCol);
        }
        if (loopAt >= 0 && loopAt < columns) {
            const float x = cellsOrigin.x + colX[static_cast<size_t>(loopAt)] - kGap * 0.5f;
            draw->AddRectFilled(ImVec2(x - 1.5f, cellsOrigin.y + choreoRowY(0) - 1.0f),
                                ImVec2(x + 1.5f, cellsOrigin.y + choreoRowY(choreoRows) - kGap + 1.0f), barCol);
        }
    }

    // ---- The cell menu: columns, and the clipboard.
    if (ImGui::BeginPopup("##cellMenu")) {
        const int b = state.menuBeat;
        const int n = count.n;
        std::string why;
        auto changedBy = [&](bool ok) {
            if (ok) {
                emit(true);
            } else if (!why.empty()) {
                note(why);
            }
        };
        if (ImGui::MenuItem("Insert Column Left")) {
            insertColumns(&grid, b, 1, count, false);
            changedBy(true);
        }
        if (ImGui::MenuItem("Insert Column Right")) {
            insertColumns(&grid, b + 1, 1, count, false);
            changedBy(true);
        }
        if (count.active()) {
            const std::string what = "a " + std::to_string(n) + "-count";
            if (ImGui::MenuItem(("Insert " + what + " Left").c_str())) {
                insertColumns(&grid, b / n * n, n, count, true);
                changedBy(true);
            }
            if (ImGui::MenuItem(("Insert " + what + " Right").c_str())) {
                insertColumns(&grid, (b / n + 1) * n, n, count, true);
                changedBy(true);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Column")) changedBy(deleteColumns(&grid, b, 1, &why));
        // Everything from this beat to the end (a pattern written out longer than it needs).
        if (ImGui::MenuItem(("Delete Beat " + std::to_string(b + 1) + " and All After").c_str(), nullptr, false, b > 0))
            changedBy(deleteColumns(&grid, b, grid.period - b, &why));
        if (b == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("A pattern needs at least one beat.");
        // ...or just down to where it starts repeating.
        const int shortest = shortestGridPeriod(grid, count);
        if (ImGui::MenuItem(("Shorten to Repeat (period " + std::to_string(shortest) + ")").c_str(), nullptr, false,
                            shortest < grid.period))
            changedBy(deleteColumns(&grid, shortest, grid.period - shortest, &why));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (shortest < grid.period)
                ImGui::SetTooltip("The pattern repeats every %d beats: keep just the first %d.", shortest, shortest);
            else
                ImGui::SetTooltip("The pattern doesn't repeat within its %d beats.", grid.period);
        }
        if (count.active() && ImGui::MenuItem(("Delete This " + std::to_string(n) + "-count").c_str()))
            changedBy(deleteColumns(&grid, b / n * n, n, &why));
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", "Ctrl+X")) {
            copy();
            clearSelection();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C")) copy();
        if (ImGui::MenuItem("Paste", "Ctrl+V")) paste();
        if (ImGui::MenuItem("Clear", "Delete")) clearSelection();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    hoveredAny = hoveredAny || ImGui::IsItemHovered();

    // ---- Row headers, drawn.
    {
        ImDrawList* draw2 = ImGui::GetWindowDrawList();
        for (int r = 0; r < rows; ++r) {
            const ImVec2 a(origin.x, origin.y + rowY(r)), z(origin.x + headerW, origin.y + rowY(r) + cellH);
            const bool cursorRow = r == state.row;
            draw2->AddRectFilled(a, z, ImGui::GetColorU32(cursorRow && state.focused ? ImGuiCol_Header : ImGuiCol_FrameBg, 0.8f));
            draw2->AddText(ImVec2(a.x + kPadX, a.y + (cellH - ImGui::GetTextLineHeight()) * 0.5f),
                           ImGui::GetColorU32(grid.rows[static_cast<size_t>(r)].linked() ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                           headers[static_cast<size_t>(r)].c_str());
            // Who's on it now, if it's someone else.
            const int who = r < static_cast<int>(throwLoop.who.size()) ? throwLoop.who[static_cast<size_t>(r)] : -1;
            if (who >= 0) {
                const std::string tag = "J" + std::to_string(who + 1);
                draw2->AddText(ImVec2(z.x - kPadX - ImGui::CalcTextSize(tag.c_str()).x, a.y + (cellH - ImGui::GetTextLineHeight()) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_TextDisabled), tag.c_str());
            }
        }
        // The choreography's: a thin line above them says what they are.
        if (hasChoreo) {
            const float y = origin.y + rowY(rows) + sectionGap() * 0.5f - kGap;
            draw2->AddLine(ImVec2(origin.x, y), ImVec2(rightEdge, y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        }
        for (int k = 0; k < choreoRows; ++k) {
            const ImVec2 a(origin.x, origin.y + choreoRowY(k)), z(origin.x + headerW, origin.y + choreoRowY(k) + cellH);
            const bool cursorRow = state.inChoreo && k == state.choreoRow;
            draw2->AddRectFilled(a, z, ImGui::GetColorU32(cursorRow && state.focused ? ImGuiCol_Header : ImGuiCol_FrameBg, 0.8f));
            const bool linked = !choreo.rows[static_cast<size_t>(k)].link.empty();
            draw2->AddText(ImVec2(a.x + kPadX, a.y + (cellH - ImGui::GetTextLineHeight()) * 0.5f),
                           linked ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : IM_COL32(236, 226, 150, 255),
                           choreoHeaders[static_cast<size_t>(k)].c_str());
            const int who = k < static_cast<int>(choreo.who.size()) ? choreo.who[static_cast<size_t>(k)] : -1;
            if (who >= 0) {
                const std::string tag = "J" + std::to_string(who + 1);
                draw2->AddText(ImVec2(z.x - kPadX - ImGui::CalcTextSize(tag.c_str()).x, a.y + (cellH - ImGui::GetTextLineHeight()) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_TextDisabled), tag.c_str());
            }
        }
    }

    // ---- The next column, beside the row names: for each row, which row whoever's on it goes on
    // to after the period (throws) or the walking's length (and how far turned); its own, dim.
    // Red if it's part of a problem. Click to type there; right-click to choose (throws) or for a
    // builder (walking).
    if (hasNextColumn) {
        ImDrawList* draw3 = ImGui::GetWindowDrawList();
        const ImU32 frameBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 dimText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 errorBg = errorCellColor(colorVision);
        {
            const char* head = "next";
            const ImVec2 hs = ImGui::CalcTextSize(head);
            draw3->AddText(ImVec2(nextX + (loopW - hs.x) * 0.5f, origin.y), dimText, head);
        }
        auto cellBox = [&](float y, const std::string& shown, bool own, bool bad, bool cursor, bool typing) {
            const ImVec2 a(nextX, y), z(nextX + loopW, y + cellH);
            draw3->AddRectFilled(a, z, bad && !typing ? errorBg : frameBg);
            const ImU32 fg = bad && !typing ? IM_COL32(0, 0, 0, 255) : (own && !typing ? dimText : textCol);
            const ImVec2 size = ImGui::CalcTextSize(shown.c_str());
            draw3->AddText(ImVec2(a.x + (loopW - size.x) * 0.5f, a.y + (cellH - size.y) * 0.5f), fg, shown.c_str());
            if (typing && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
                const float cx = a.x + (loopW + size.x) * 0.5f + 1.0f;
                draw3->AddLine(ImVec2(cx, a.y + 2.0f), ImVec2(cx, z.y - 2.0f), textCol, 1.0f);
            }
            if (cursor)
                draw3->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(z.x + 1.0f, z.y + 1.0f),
                               state.focused ? textCol : dimText, 0.0f, 0, state.focused ? 2.0f : 1.0f);
        };
        // The throws'.
        for (int r = 0; r < rows && hasThrowNext; ++r) {
            const float y = origin.y + rowY(r);
            ImGui::SetCursorScreenPos(ImVec2(nextX, y));
            ImGui::PushID(2000 + r);
            ImGui::InvisibleButton("##throwNext", ImVec2(loopW, cellH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            ImGui::PopID();
            const bool hovered = ImGui::IsItemHovered();
            hoveredAny = hoveredAny || hovered;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                commitEdit();
                if (state.inChoreo && !state.choreoTyped.empty()) choreoTake();
                state.focused = true;
                state.inChoreo = false;
                state.inThrowNext = true;
                state.row = state.anchorRow = r;
                state.anchorBeat = state.beat;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    state.throwNextMenuRow = r;
                    openThrowNextMenu = true;
                }
            }
            const std::string next = r < static_cast<int>(throwLoop.next.size()) ? throwLoop.next[static_cast<size_t>(r)] : "";
            const bool bad = r < static_cast<int>(throwLoop.bad.size()) && throwLoop.bad[static_cast<size_t>(r)] != 0;
            cellBox(y, next.empty() ? std::to_string(r + 1) : next, next.empty(), bad, state.inThrowNext && r == state.row, false);
            if (hovered)
                ImGui::SetTooltip("After beat %d, whoever's on %s goes on to T%s's throws (from its beat 1).\n"
                                  "Type a row's number; Backspace for its own. Right-click to choose.\n"
                                  "A pass goes to whoever is on its target's C row when it lands; a self stays a self.",
                                  grid.period, tName(r).c_str(), next.empty() ? std::to_string(r + 1).c_str() : next.c_str());
        }
        // The walking's.
        for (int k = 0; k < choreoRows; ++k) {
            const float y = origin.y + choreoRowY(k);
            ImGui::SetCursorScreenPos(ImVec2(nextX, y));
            ImGui::PushID(3000 + k);
            ImGui::InvisibleButton("##loopNext", ImVec2(loopW, cellH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            ImGui::PopID();
            const bool hovered = ImGui::IsItemHovered();
            hoveredAny = hoveredAny || hovered;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                commitEdit();
                state.focused = true;
                loopGo(k);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) res.loopBuilderJuggler = k;
            }
            const ChoreoGridRow& row = choreo.rows[static_cast<size_t>(k)];
            const bool cursorHere = state.inChoreo && state.inLoop && k == state.choreoRow;
            const bool typing = cursorHere && !state.choreoTyped.empty();
            cellBox(y, typing ? state.choreoTyped : (row.loop.empty() ? std::to_string(k + 1) : row.loop), row.loop.empty(),
                    row.loopBad, cursorHere, typing);
            if (hovered)
                ImGui::SetTooltip("After beat %d, whoever's on %s goes on to the C row named here, from its beat 1:\n"
                                  "type which (2: C2), and a turn if it's turned (2r+1/4: a quarter turn counterclockwise;\n"
                                  "r1/2: its own path, a half turn). Right-click for a builder.%s%s",
                                  choreo.written, cName(k).c_str(), choreo.loopProblem.empty() ? "" : "\n\nProblem: ",
                                  choreo.loopProblem.c_str());
        }
    }

    // ---- A juggler's walking menu (the choreography's rows).
    // ---- The throws' next column: whose throws a juggler takes up after the period.
    if (openThrowNextMenu) ImGui::OpenPopup("##throwNextMenu");
    if (ImGui::BeginPopup("##throwNextMenu")) {
        const int r = state.throwNextMenuRow;
        ImGui::TextDisabled("After beat %d, whoever's on %s goes on to:", grid.period, tName(r).c_str());
        const int current = r < static_cast<int>(throwLoop.next.size()) && !throwLoop.next[static_cast<size_t>(r)].empty()
                                ? std::atoi(throwLoop.next[static_cast<size_t>(r)].c_str()) - 1
                                : r;
        for (int k = 0; k < rows; ++k) {
            if (k > 0) ImGui::SameLine();
            char label[24];
            std::snprintf(label, sizeof(label), k == r ? "T%d (own)" : "T%d", k + 1);
            const bool chosen = k == current;
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label)) {
                res.throwNextEdits.push_back({r, k == r ? -1 : k});
                ImGui::CloseCurrentPopup();
            }
            if (chosen) ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    if (openChoreoMenu) ImGui::OpenPopup("##choreoRowMenu");
    if (ImGui::BeginPopup("##choreoRowMenu")) {
        const int k = state.choreoMenuRow;
        ImGui::TextDisabled("%s's walking", cName(k).c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Walk Like...")) res.walkLikeJuggler = k;
        const bool linked = k < choreoRows && !choreo.rows[static_cast<size_t>(k)].link.empty();
        if (ImGui::MenuItem("Unlink (write the walking out)", nullptr, false, linked)) res.walkUnlinkJuggler = k;
        if (ImGui::MenuItem("Clear This Keyframe", "Delete", false,
                            state.inChoreo && !state.inLoop && k == state.choreoRow && !choreoCell(k, state.choreoBeat).text.empty() &&
                                choreoEditable(k, state.choreoBeat, false)))
            choreoClear();
        // Beats of the walking (everyone's): the keyframes after move along.
        const int b = state.choreoMenuBeat;
        if (b >= 0) {
            ImGui::Separator();
            if (ImGui::MenuItem(("Insert a Beat Before " + std::to_string(b + 1)).c_str(), nullptr, false, b <= choreo.written)) {
                ChoreoEdit e;
                e.kind = ChoreoEdit::Kind::InsertBeat;
                e.beat = b;
                res.choreoEdits.push_back(e);
            }
            if (ImGui::MenuItem(("Insert a Beat After " + std::to_string(b + 1)).c_str(), nullptr, false, b < choreo.written)) {
                ChoreoEdit e;
                e.kind = ChoreoEdit::Kind::InsertBeat;
                e.beat = b + 1;
                res.choreoEdits.push_back(e);
            }
            if (ImGui::MenuItem(("Delete Beat " + std::to_string(b + 1)).c_str(), nullptr, false,
                                b < choreo.written && choreo.written > 1)) {
                ChoreoEdit e;
                e.kind = ChoreoEdit::Kind::DeleteBeat;
                e.beat = b;
                res.choreoEdits.push_back(e);
            }
            if (b >= choreo.written && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Beat %d is a copy (the loop): only beats 1-%d are written out.", b + 1, choreo.written);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && b < choreo.written)
                ImGui::SetTooltip("Everyone's walking: their keyframes on this beat go, and the later ones move up.");
        }
        ImGui::EndPopup();
    }

    // ---- A juggler's menu.
    if (ImGui::BeginPopup("##rowMenu")) {
        const int r = state.menuRow;
        std::string why;
        auto changedBy = [&](bool ok) {
            if (ok)
                emit(true);
            else if (!why.empty())
                note(why);
        };
        if (ImGui::MenuItem("Add Juggler Above", nullptr, false, rows < kMaxJugglers)) {
            changedBy(addJuggler(&grid, r, count, &why));
            state.row = state.anchorRow = r;
        }
        if (ImGui::MenuItem("Add Juggler Below", nullptr, false, rows < kMaxJugglers)) {
            changedBy(addJuggler(&grid, r + 1, count, &why));
            state.row = state.anchorRow = r + 1;
        }
        if (ImGui::MenuItem(("Remove " + tName(r) + " (and a juggler)").c_str(), nullptr, false, rows > 1))
            changedBy(removeJuggler(&grid, r, &why));
        if (rows > 1) {
            ImGui::Separator();
            // Same as: another juggler, from one of their beats (a link, "@1[24]"). Starts that leave
            // the pattern with an error are marked, not refused: it can take several links to
            // come right.
            const JugglingLoop& loop = parsed.loop;
            const bool canLink = !loop.empty() && loop.jugglers == rows;
            if (ImGui::BeginMenu("Same as", canLink)) {
                for (int to = 0; to < rows; ++to) {
                    if (to == r || !ImGui::BeginMenu(tName(to).c_str())) continue;
                    for (int d = 0; d < loop.period; ++d) {
                        PatternForm newForm;
                        JugglingLoop newLoop;
                        std::string problem;
                        const bool ok = linkJuggler(parsed.form, loop, r, to, d, &newForm, &newLoop, &problem, true);
                        const GridCell first = derivedCell(parsed, to, d);
                        const std::string item = "From beat " + std::to_string(d + 1) + ", " +
                                                 (first.blank() ? std::string("blank") : first.text()) + " (" +
                                                 linkText(to, d, loop.period) + ")" +
                                                 (ok && !problem.empty() ? "  - error" : "");
                        if (ImGui::MenuItem(item.c_str(), nullptr, false, ok)) {
                            res.linkJuggler = r;
                            res.linkTo = to;
                            res.linkStart = d;
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !problem.empty())
                            ImGui::SetTooltip("%s%s", ok ? "Leaves an error (more links or edits can fix it): " : "Can't: ",
                                              problem.c_str());
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (!canLink && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("The pattern has to be readable first (each juggler's throws, no mistakes in the notation).");
            if (ImGui::MenuItem("Unlink (write the throws out)", nullptr, false, grid.rows[static_cast<size_t>(r)].linked()))
                res.unlinkJuggler = r;
        }
        const bool swapped = grid.rows[static_cast<size_t>(r)].options.find("LRswap") != std::string::npos;
        if (ImGui::MenuItem("Swap Hands (LRswap)", nullptr, swapped)) {
            toggleLrSwap(&grid, r);
            emit(true);
        }
        ImGui::EndPopup();
    }

    // ---- The line under the grid: the count directive.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + gridH + style.ScrollbarSize + style.ItemSpacing.y));
    const std::string countLabel = (count.active() ? std::to_string(count.n) + "-count" : std::string("No count")) + "##count";
    if (ImGui::SmallButton(countLabel.c_str())) {
        commitEdit();
        ImGui::OpenPopup("##countMenu");
    }
    hoveredAny = hoveredAny || ImGui::IsItemHovered();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A count directive: with a 4-count only beats 1, 5, 9, ... are edited,\n"
                          "and the 3s between are greyed out and skipped.");
    if (ImGui::BeginPopup("##countMenu")) {
        int chosen = -1;
        if (ImGui::MenuItem("No count", nullptr, !count.active())) chosen = 0;
        for (int n = 2; n <= kMaxCount; ++n)
            if (ImGui::MenuItem((std::to_string(n) + "-count").c_str(), nullptr, count.n == n)) chosen = n;
        ImGui::EndPopup();
        if (chosen >= 0 && chosen != count.n) {
            if (chosen == 0 || gridFitsCount(grid, chosen)) {
                res.newCount = chosen;
            } else {
                state.countWanted = chosen;
                state.askCount = true;
            }
        }
    }

    // ---- Dialogs.
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (state.askCount) {
        ImGui::OpenPopup("Change the count");
        state.askCount = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Change the count", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = state.countWanted;
        ImGui::Text("Make this a %d-count?", n);
        bool between = false;  // a beat between the count's beats that isn't the filler
        for (int r = 0; r < rows; ++r)
            for (int b = 0; b < grid.period; ++b)
                if (b % std::max(1, n) != 0 && !grid.rows[static_cast<size_t>(r)].linked() &&
                    cellAt(r, b) != selfCell(count.filler))
                    between = true;
        const bool period = grid.period % std::max(1, n) != 0;
        if (between)
            ImGui::TextDisabled("Some beats between the %d-count's beats aren't 3s%s.", n,
                                period ? ", and the period isn't a multiple of the count" : "");
        else
            ImGui::TextDisabled("The period (%d) isn't a multiple of %d.", grid.period, n);
        ImGui::Spacing();
        ImGui::Text("Fill In: the beats that matter go to beats 1, %d, %d, ..., with 3s between.", n + 1, 2 * n + 1);
        ImGui::TextUnformatted("Keep Throws: everything stays where it is, and what's off the count shows as errors.");
        ImGui::Spacing();
        if (ImGui::Button("Fill In")) {
            grid = gridWithCountFilledIn(grid, count, n);
            emit(true);
            res.newCount = n;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Throws")) {
            grid = gridWithCountKept(grid, n);
            emit(true);
            res.newCount = n;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (state.askPasteWhere) {
        ImGui::OpenPopup("Paste where?");
        state.askPasteWhere = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Paste where?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int b = state.paste.beat, n = std::max(1, count.n), k = state.paste.clip.period;
        ImGui::Text("This is a %d-count. Paste the %d beats", n, k);
        if (ImGui::Button(("Into beats " + std::to_string(b + 1) + ", " + std::to_string(b + 2) + ", " +
                           std::to_string(b + 3) + ", ...").c_str())) {
            state.paste.step = 1;
            ImGui::CloseCurrentPopup();
            continuePaste();
        }
        ImGui::SameLine();
        if (ImGui::Button(("Onto the count: beats " + std::to_string(b + 1) + ", " + std::to_string(b + n + 1) + ", " +
                           std::to_string(b + 2 * n + 1) + ", ...").c_str())) {
            state.paste.step = n;
            ImGui::CloseCurrentPopup();
            continuePaste();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            state.paste.active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (state.askPasteExtend) {
        ImGui::OpenPopup("Paste past the end?");
        state.askPasteExtend = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Paste past the end?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const SiteswapGridState::Paste& p = state.paste;
        const int needPeriod = p.beat + (p.clip.period - 1) * p.step + 1;
        const int needRows = p.row + p.clip.jugglers();
        std::string what;
        if (needPeriod > grid.period) what = "beats up to " + std::to_string(needPeriod);
        if (needRows > grid.jugglers()) what += (what.empty() ? "" : " and ") + std::string("jugglers up to J") + std::to_string(needRows);
        ImGui::Text("The paste goes past the end of the pattern (it needs %s).", what.c_str());
        if (ImGui::Button("Make the Pattern Bigger")) {
            ImGui::CloseCurrentPopup();
            finishPaste(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Paste") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            state.paste.active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Clicking anywhere else gives the keyboard back (finishing any throw being typed).
    const bool anyPopup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    hoveredAny = ImGui::IsMouseHoveringRect(origin, ImVec2(rightEdge, origin.y + gridH + style.ScrollbarSize + style.ItemSpacing.y + ImGui::GetFrameHeight())) &&
                 ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (state.focused && !hoveredAny && !anyPopup &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        commitEdit();
        if (state.inChoreo && !state.choreoTyped.empty()) choreoTake();
        state.focused = false;
    }

    // Where the cursor is, for the status line.
    if (state.focused && state.inThrowNext && hasThrowNext) {
        const int r = state.row;
        const std::string next = r < static_cast<int>(throwLoop.next.size()) ? throwLoop.next[static_cast<size_t>(r)] : "";
        res.readout = tName(r) + " after beat " + std::to_string(grid.period) + ": " +
                      (next.empty() ? std::string("its own throws again") : "on to T" + next + "'s throws");
    } else if (state.focused && state.inChoreo && state.inLoop && hasChoreo) {
        const int k = state.choreoRow;
        const std::string& loop = choreo.rows[static_cast<size_t>(k)].loop;
        res.readout = cName(k) + " after beat " + std::to_string(choreo.written) + ": " +
                      (loop.empty() ? std::string("its own path again") : "on to " + loop);
    } else if (state.focused && state.inChoreo && hasChoreo) {
        const int k = state.choreoRow, b = state.choreoBeat;
        std::string out = cName(k) + "'s walking, beat " + std::to_string(b + 1) + " [" + std::to_string(b) + "]";
        const ChoreoGridCell& cell = choreoCell(k, b);
        if (!cell.text.empty())
            out += ": arrives on mark " + cell.text.substr(0, 2) + (cell.text.size() > 2 ? ", the long way round" : "");
        else if (cell.stay)
            out += ": standing still";
        else
            out += ": walking";
        if (cell.locked || b >= choreo.written) out += " (a copy)";
        res.readout = out;
    } else if (state.focused) {
        const int r = state.row, b = state.beat;
        std::string out = rows > 1 ? tName(r) + ", " : std::string();
        out += "beat " + std::to_string(b + 1);
        if (rows > 1) out += " [" + std::to_string(b) + "]";  // (as a link to it would say: "@1[4]")
        const JugglingLoop& loop = parsed.loop;
        if (!loop.empty() && r < loop.jugglers) out += loop.rightHandBeat(r, b) ? ", right hand" : ", left hand";
        const GridCell cell = grid.rows[static_cast<size_t>(r)].linked() ? derivedCell(parsed, r, b) : cellAt(r, b);
        out += ": " + (cell.blank() ? std::string("blank") : cell.text());
        res.readout = out;
    }

    ImGui::SameLine();
    ImGui::PopID();
    return res;
}
