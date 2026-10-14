// siteswap_edit.cpp - see siteswap_edit.h.
#include "siteswap_edit.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstddef>

char throwChar(int value) {
    if (value < 0 || value > 35 || value == 25 || value == 33) return 0;
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

int throwValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    const int lower = std::tolower(static_cast<unsigned char>(c));
    if (lower >= 'a' && lower <= 'z' && lower != 'p' && lower != 'x') return lower - 'a' + 10;
    return -1;
}

GridCell selfCell(int value) {
    GridCell c;
    c.value = value;
    return c;
}

std::string GridCell::text() const {
    if (blank()) return std::string();
    std::string s(1, throwChar(value));
    if (pass) {
        s += 'p';
        if (relative)
            s += (along < 0 ? "-" : "+") + std::to_string(along < 0 ? -along : along);
        else if (target > 0)
            s += std::to_string(target);
    }
    return s;
}

namespace {

// ---- Reading text

bool readGrid(const std::string& text, SiteswapGrid* grid) {
    SiteswapGrid g;
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    g.passing = i < text.size() && text[i] == '<';
    if (g.passing) ++i;
    bool closed = false;
    g.rows.emplace_back();
    while (i < text.size()) {
        const char c = text[i];
        GridRow& row = g.rows.back();
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (closed) {
            return false;  // something after the ">"
        } else if (g.passing && c == '|') {
            ++i;
            g.rows.emplace_back();
        } else if (g.passing && c == '>') {
            closed = true;
            ++i;
        } else if (c == ',') {
            // Options, up to the end of the part ("LRswap" written the standard way).
            const size_t stop = g.passing ? text.find_first_of("|>", i) : std::string::npos;
            const std::string raw = text.substr(i, stop == std::string::npos ? std::string::npos : stop - i);
            size_t from = 1;
            while (from <= raw.size()) {
                size_t next = raw.find(',', from);
                if (next == std::string::npos) next = raw.size();
                std::string option = raw.substr(from, next - from);
                while (!option.empty() && std::isspace(static_cast<unsigned char>(option.back()))) option.pop_back();
                while (!option.empty() && std::isspace(static_cast<unsigned char>(option.front()))) option.erase(0, 1);
                std::string lower;
                for (const char ch : option) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower == "lrswap") option = "LRswap";
                if (!option.empty()) row.options += "," + option;
                from = next + 1;
            }
            i = stop == std::string::npos ? text.size() : stop;
        } else if (g.passing && c == '@' && row.cells.empty() && row.link.empty() && row.options.empty()) {
            std::string link = "@";
            ++i;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '[' ||
                                       text[i] == ']' || text[i] == '-' || text[i] == ' ')) {
                if (text[i] != ' ') link += text[i];
                ++i;
            }
            row.link = link;
        } else if (!row.link.empty() || !row.options.empty()) {
            return false;  // throws after a link or options
        } else if (c == '?' || throwValue(c) >= 0) {
            GridCell cell;
            cell.value = c == '?' ? kOpenThrow : throwValue(c);
            ++i;
            // (A "p" is read in a single part too: one row copied from a passing pattern.)
            if (i < text.size() && std::tolower(static_cast<unsigned char>(text[i])) == 'p') {
                cell.pass = true;
                ++i;
                if (i + 1 < text.size() && (text[i] == '+' || text[i] == '-') &&
                    std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
                    cell.relative = true;
                    cell.along = (text[i + 1] - '0') * (text[i] == '-' ? -1 : 1);
                    i += 2;
                } else if (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    cell.target = text[i] - '0';
                    ++i;
                }
            }
            row.cells.push_back(cell);
        } else {
            return false;
        }
    }
    // With two jugglers, a digit after a "p" is the next throw ("3p3" is 3p, then 3).
    if (g.passing && g.rows.size() == 2) {
        for (GridRow& row : g.rows) {
            for (size_t k = 0; k < row.cells.size(); ++k) {
                if (!row.cells[k].pass || row.cells[k].target <= 0) continue;
                const GridCell next = selfCell(row.cells[k].target);
                row.cells[k].target = 0;
                row.cells.insert(row.cells.begin() + static_cast<std::ptrdiff_t>(k) + 1, next);
            }
        }
    }
    for (const GridRow& row : g.rows)
        if (!row.linked()) g.period = std::max(g.period, static_cast<int>(row.cells.size()));
    for (GridRow& row : g.rows)
        if (!row.linked()) row.cells.resize(static_cast<size_t>(g.period));
    *grid = g;
    return true;
}

bool isFiller(const GridCell& cell, const GridCount& count) {
    return cell == selfCell(count.filler);
}

// Follows a link's text: "@2[3]" -> to 1 (0-based), start 3. False if it isn't one.
bool readLink(const std::string& link, int* to, int* start) {
    if (link.size() < 2 || link[0] != '@') return false;
    size_t i = 1;
    int number = 0;
    bool any = false;
    while (i < link.size() && std::isdigit(static_cast<unsigned char>(link[i]))) {
        number = number * 10 + (link[i] - '0');
        any = true;
        ++i;
    }
    if (!any) return false;
    int value = 0;
    if (i < link.size()) {
        if (link[i] != '[') return false;
        ++i;
        const bool negative = i < link.size() && link[i] == '-';
        if (negative) ++i;
        while (i < link.size() && std::isdigit(static_cast<unsigned char>(link[i]))) value = value * 10 + (link[i++] - '0');
        if (negative) value = -value;
    }
    *to = number - 1;
    *start = value;
    return true;
}

std::string writeLink(int to, int start) {
    std::string s = "@" + std::to_string(to + 1);
    if (start != 0) s += "[" + std::to_string(start) + "]";
    return s;
}

// Which juggler a cell's throw goes to (thrown by `row`), or -1 if that isn't known yet.
int destinationOf(const GridCell& cell, int row, int jugglers) {
    if (!cell.pass) return row;
    if (jugglers < 1) return -1;
    if (cell.relative) return ((row + cell.along) % jugglers + jugglers) % jugglers;
    if (cell.target >= 1 && cell.target <= jugglers) return cell.target - 1;
    return jugglers == 2 ? 1 - row : -1;
}

}  // namespace

bool gridFromText(const std::string& text, SiteswapGrid* grid) { return readGrid(text, grid); }

std::string gridToText(const SiteswapGrid& grid) {
    std::string out;
    bool passing = grid.passing || grid.rows.size() > 1;
    for (const GridRow& row : grid.rows)
        for (const GridCell& c : row.cells) passing = passing || c.pass;
    if (passing) out += '<';
    for (size_t r = 0; r < grid.rows.size(); ++r) {
        const GridRow& row = grid.rows[r];
        if (r > 0) out += '|';
        if (row.linked()) {
            out += row.link;
        } else {
            for (size_t b = 0; b < row.cells.size(); ++b) {
                if (b > 0 && passing) out += ' ';
                out += row.cells[b].blank() ? "?" : row.cells[b].text();
            }
        }
        out += row.options;
    }
    if (passing) out += '>';
    return out;
}

bool isFillerCell(const SiteswapGrid& grid, int row, int beat, const GridCount& count) {
    if (!count.active() || count.countBeat(beat) || row < 0 || row >= grid.jugglers()) return false;
    const GridRow& r = grid.rows[static_cast<size_t>(row)];
    if (r.linked() || beat < 0 || beat >= static_cast<int>(r.cells.size())) return false;
    return isFiller(r.cells[static_cast<size_t>(beat)], count);
}

bool gridCellSource(const SiteswapGrid& grid, int row, int beat, int* sourceRow, int* sourceBeat) {
    if (row < 0 || row >= grid.jugglers() || grid.period <= 0) return false;
    int r = row, b = beat;
    for (int steps = 0; grid.rows[static_cast<size_t>(r)].linked(); ++steps) {
        int to = 0, start = 0;
        if (steps > grid.jugglers() || !readLink(grid.rows[static_cast<size_t>(r)].link, &to, &start) || to < 0 ||
            to >= grid.jugglers())
            return false;
        r = to;
        b += start;
    }
    *sourceRow = r;
    *sourceBeat = ((b % grid.period) + grid.period) % grid.period;
    return true;
}

bool isEditableCell(const SiteswapGrid& grid, int row, int beat, const GridCount& count) {
    if (row < 0 || row >= grid.jugglers() || beat < 0 || beat >= grid.period) return false;
    return !grid.rows[static_cast<size_t>(row)].linked() && !isFillerCell(grid, row, beat, count);
}

bool gridFitsCount(const SiteswapGrid& grid, int n) {
    if (n < 2) return true;
    if (grid.period % n != 0) return false;
    GridCount count;
    count.n = n;
    for (const GridRow& row : grid.rows) {
        if (row.linked()) continue;
        for (int b = 0; b < grid.period; ++b)
            if (!count.countBeat(b) && !isFiller(row.cells[static_cast<size_t>(b)], count)) return false;
    }
    return true;
}

SiteswapGrid gridWithCountFilledIn(const SiteswapGrid& grid, const GridCount& from, int n) {
    GridCount to;
    to.n = n;
    to.filler = from.filler;
    // The beats that matter: the old count's, and any that isn't all filler.
    std::vector<int> beats;
    for (int b = 0; b < grid.period; ++b) {
        bool matters = from.active() && from.countBeat(b);
        for (const GridRow& row : grid.rows)
            if (!row.linked() && !isFiller(row.cells[static_cast<size_t>(b)], to)) matters = true;
        if (matters) beats.push_back(b);
    }
    if (beats.empty() && grid.period > 0) beats.push_back(0);
    SiteswapGrid out = grid;
    out.period = n * static_cast<int>(beats.size());
    for (GridRow& row : out.rows) {
        if (row.linked()) continue;
        const std::vector<GridCell> was = row.cells;
        row.cells.assign(static_cast<size_t>(out.period), selfCell(to.filler));
        for (size_t k = 0; k < beats.size(); ++k) row.cells[k * static_cast<size_t>(n)] = was[static_cast<size_t>(beats[k])];
    }
    return out;
}

SiteswapGrid gridWithCountKept(const SiteswapGrid& grid, int n) {
    SiteswapGrid out = grid;
    GridCount count;
    count.n = n;
    const int period = std::max(n, (grid.period + n - 1) / n * n);
    extendGrid(&out, period, out.jugglers(), count);
    return out;
}

bool countAllowsChange(const SiteswapGrid& before, const SiteswapGrid& after, const GridCount& count,
                       std::string* why) {
    if (!count.active()) return true;
    const int n = count.n;
    if (after.period % n != 0) {
        if (why)
            *why = "this is a " + std::to_string(n) + "-count: beats go in and out " + std::to_string(n) + " at a time";
        return false;
    }
    for (int r = 0; r < after.jugglers(); ++r) {
        const GridRow& row = after.rows[static_cast<size_t>(r)];
        if (row.linked()) continue;
        for (int b = 0; b < after.period; ++b) {
            if (count.countBeat(b) || isFiller(row.cells[static_cast<size_t>(b)], count)) continue;
            // (A beat that was already off the count may stay as it was.)
            if (r < before.jugglers() && b < before.period && !before.rows[static_cast<size_t>(r)].linked() &&
                before.rows[static_cast<size_t>(r)].cells[static_cast<size_t>(b)] == row.cells[static_cast<size_t>(b)])
                continue;
            if (why)
                *why = "beat " + std::to_string(b + 1) + " is between the " + std::to_string(n) +
                       "-count's beats (only beats 1, " + std::to_string(n + 1) + ", " + std::to_string(2 * n + 1) +
                       ", ... can change)";
            return false;
        }
    }
    return true;
}

int countPeriodFor(int shortestPeriod, const GridCount& count) {
    if (!count.active() || shortestPeriod <= 0) return shortestPeriod;
    int period = shortestPeriod;
    while (period % count.n != 0) period += shortestPeriod;
    return period;
}

void insertColumns(SiteswapGrid* grid, int at, int columns, const GridCount& count, bool wholeCount) {
    at = std::clamp(at, 0, grid->period);
    for (GridRow& row : grid->rows) {
        if (row.linked()) continue;
        std::vector<GridCell> added;
        for (int k = 0; k < columns; ++k)
            added.push_back(wholeCount && count.active() && k % count.n != 0 ? selfCell(count.filler) : GridCell());
        row.cells.insert(row.cells.begin() + at, added.begin(), added.end());
    }
    grid->period += columns;
}

bool deleteColumns(SiteswapGrid* grid, int at, int columns, std::string* why) {
    if (at < 0 || columns <= 0 || at + columns > grid->period) return false;
    if (columns >= grid->period) {
        if (why) *why = "A pattern needs at least one beat.";
        return false;
    }
    for (GridRow& row : grid->rows)
        if (!row.linked()) row.cells.erase(row.cells.begin() + at, row.cells.begin() + at + columns);
    grid->period -= columns;
    return true;
}

int shortestGridPeriod(const SiteswapGrid& grid, const GridCount& count) {
    for (int p = 1; p < grid.period; ++p) {
        if (grid.period % p != 0 || (count.active() && p % count.n != 0)) continue;
        bool repeats = true;
        for (const GridRow& row : grid.rows) {
            if (row.linked()) continue;
            for (int b = p; b < grid.period && repeats; ++b)
                repeats = row.cells[static_cast<size_t>(b)] == row.cells[static_cast<size_t>(b % p)];
            if (!repeats) break;
        }
        if (repeats) return p;
    }
    return grid.period;
}

void extendGrid(SiteswapGrid* grid, int period, int jugglers, const GridCount& count) {
    auto fresh = [&](int beat) { return count.active() && !count.countBeat(beat) ? selfCell(count.filler) : GridCell(); };
    if (period > grid->period) {
        for (GridRow& row : grid->rows)
            if (!row.linked())
                for (int b = grid->period; b < period; ++b) row.cells.push_back(fresh(b));
        grid->period = period;
    }
    while (grid->jugglers() < std::min(jugglers, kMaxJugglers)) {
        GridRow row;
        for (int b = 0; b < grid->period; ++b) row.cells.push_back(fresh(b));
        grid->rows.push_back(row);
        grid->passing = true;
    }
}

bool addJuggler(SiteswapGrid* grid, int at, const GridCount& count, std::string* why) {
    if (grid->jugglers() >= kMaxJugglers) {
        if (why) *why = "JuggleSim handles up to " + std::to_string(kMaxJugglers) + " jugglers.";
        return false;
    }
    at = std::clamp(at, 0, grid->jugglers());
    // A bare "p" between two jugglers means the other one; with a third it needs saying.
    if (grid->jugglers() == 2)
        for (int r = 0; r < 2; ++r)
            for (GridCell& c : grid->rows[static_cast<size_t>(r)].cells)
                if (c.pass && !c.relative && c.target == 0) c.target = 2 - r;
    // Links and absolute targets to the jugglers after it move along one.
    for (GridRow& row : grid->rows) {
        int to = 0, start = 0;
        if (row.linked() && readLink(row.link, &to, &start) && to >= at) row.link = writeLink(to + 1, start);
        for (GridCell& c : row.cells)
            if (c.pass && !c.relative && c.target > at) ++c.target;
    }
    GridRow row;
    GridCount c = count;
    for (int b = 0; b < grid->period; ++b)
        row.cells.push_back(c.active() && !c.countBeat(b) ? selfCell(c.filler) : GridCell());
    grid->rows.insert(grid->rows.begin() + at, row);
    grid->passing = true;
    return true;
}

bool removeJuggler(SiteswapGrid* grid, int row, std::string* why) {
    if (row < 0 || row >= grid->jugglers()) return false;
    if (grid->jugglers() <= 1) {
        if (why) *why = "A pattern needs a juggler.";
        return false;
    }
    for (int r = 0; r < grid->jugglers(); ++r) {
        int to = 0, start = 0;
        if (r != row && grid->rows[static_cast<size_t>(r)].linked() &&
            readLink(grid->rows[static_cast<size_t>(r)].link, &to, &start) && to == row) {
            if (why)
                *why = "J" + std::to_string(r + 1) + " copies J" + std::to_string(row + 1) + ": unlink J" +
                       std::to_string(r + 1) + " first.";
            return false;
        }
    }
    grid->rows.erase(grid->rows.begin() + row);
    for (GridRow& r : grid->rows) {
        int to = 0, start = 0;
        if (r.linked() && readLink(r.link, &to, &start) && to > row) r.link = writeLink(to - 1, start);
        for (GridCell& c : r.cells)
            if (c.pass && !c.relative && c.target > row + 1) --c.target;
    }
    if (grid->jugglers() == 1) {
        bool anyPass = grid->rows[0].linked();
        for (const GridCell& c : grid->rows[0].cells) anyPass = anyPass || c.pass;
        grid->passing = anyPass;
    }
    return true;
}

void toggleLrSwap(SiteswapGrid* grid, int row) {
    if (row < 0 || row >= grid->jugglers()) return;
    std::string& options = grid->rows[static_cast<size_t>(row)].options;
    const size_t at = options.find(",LRswap");
    if (at != std::string::npos)
        options.erase(at, 7);
    else
        options += ",LRswap";
}

bool bumpCell(GridCell* cell, int delta) {
    GridCell changed = *cell;
    if (cell->blank()) {
        if (delta < 0) return false;
        changed.value = 0;
    } else {
        int v = cell->value + delta;
        while (v >= 0 && v <= 35 && throwChar(v) == 0) v += delta;
        if (v > 35) return false;
        changed.value = v < 0 ? kOpenThrow : v;
    }
    if (changed.value <= 0) {
        changed.pass = changed.relative = false;
        changed.target = changed.along = 0;
    }
    *cell = changed;
    return true;
}

bool stepCellTarget(const SiteswapGrid& grid, int row, int beat, int delta, GridCell* out, std::string* why) {
    const int jugglers = grid.jugglers();
    if (jugglers < 2) {
        if (why) *why = "Only a passing pattern's throws can go to another juggler.";
        return false;
    }
    const GridCell& cell = grid.rows[static_cast<size_t>(row)].cells[static_cast<size_t>(beat)];
    if (cell.blank() || cell.value == 0) {
        if (why) *why = cell.value == 0 ? "A 0 (an empty hand) can't be a pass." : "Decide the throw first.";
        return false;
    }
    bool relativeStyle = false;
    for (const GridRow& r : grid.rows)
        for (const GridCell& c : r.cells) relativeStyle = relativeStyle || c.relative;
    int dest = destinationOf(cell, row, jugglers);
    if (dest < 0) dest = row;
    const int along = ((dest - row) % jugglers + jugglers) % jugglers;
    const int next = ((along + delta) % jugglers + jugglers) % jugglers;
    GridCell changed = selfCell(cell.value);
    if (next != 0) {
        changed.pass = true;
        if (relativeStyle) {
            changed.relative = true;
            changed.along = next;
        } else if (jugglers > 2) {
            changed.target = (row + next) % jugglers + 1;
        }
    }
    *out = changed;
    return true;
}

bool swapCells(SiteswapGrid* grid, int row, int first, int second, int d, std::string* why) {
    GridRow& r = grid->rows[static_cast<size_t>(row)];
    const GridCell a = r.cells[static_cast<size_t>(first)];
    const GridCell b = r.cells[static_cast<size_t>(second)];
    if (a.blank() || b.blank()) {
        if (why) *why = "A throw that isn't decided yet can't be swapped.";
        return false;
    }
    GridCell newFirst = b;
    newFirst.value = b.value + d;
    GridCell newSecond = a;
    newSecond.value = a.value - d;
    if (newSecond.value < 0) {
        if (why)
            *why = "Can't swap: the second throw would have to land before it's thrown (" + std::to_string(a.value) +
                   " - " + std::to_string(d) + " < 0).";
        return false;
    }
    for (const GridCell* c : {&newFirst, &newSecond}) {
        if (throwChar(c->value) == 0) {
            if (why)
                *why = "Can't swap: a throw would become a " + std::to_string(c->value) +
                       (c->value > 35 ? ", higher than siteswap can write (35)."
                                      : ", which siteswap can't write (its letter means passing or sync).");
            return false;
        }
        if (c->value == 0 && c->pass && destinationOf(*c, row, grid->jugglers()) != row) {
            if (why) *why = "Can't swap: a pass would become a 0 (an empty hand can't be a pass).";
            return false;
        }
    }
    r.cells[static_cast<size_t>(first)] = newFirst;
    r.cells[static_cast<size_t>(second)] = newSecond;
    return true;
}

bool rotateGrid(SiteswapGrid* grid, int steps) {
    const int n = grid->period;
    if (n < 2) return false;
    const int shift = ((steps % n) + n) % n;
    if (shift == 0) return false;
    for (GridRow& row : grid->rows)
        if (!row.linked()) std::rotate(row.cells.begin(), row.cells.begin() + (n - shift), row.cells.end());
    return true;
}

std::string cellsToText(const SiteswapGrid& grid, int row0, int row1, int beat0, int beat1) {
    SiteswapGrid part;
    part.passing = grid.passing || grid.jugglers() > 1;
    for (int r = row0; r <= row1; ++r) {
        if (r < 0 || r >= grid.jugglers()) continue;
        GridRow row;
        const GridRow& from = grid.rows[static_cast<size_t>(r)];
        for (int b = beat0; b <= beat1; ++b)
            if (b >= 0 && b < static_cast<int>(from.cells.size())) row.cells.push_back(from.cells[static_cast<size_t>(b)]);
        part.rows.push_back(row);
    }
    // One row of a passing pattern is written without its brackets: "4p 3 3".
    if (part.rows.size() == 1 && part.passing) {
        std::string out;
        for (size_t b = 0; b < part.rows[0].cells.size(); ++b) {
            if (b > 0) out += ' ';
            out += part.rows[0].cells[b].blank() ? "?" : part.rows[0].cells[b].text();
        }
        return out;
    }
    return gridToText(part);
}
