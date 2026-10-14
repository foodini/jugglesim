// siteswap_edit.h - the siteswap grid: a pattern as a table of throws, one row per juggler and
// one column per beat, read from siteswap text and written back to it, plus the edits the grid
// makes (count directives, columns, jugglers, swapping, rotating, pasting).
//
// Everything here works on the grid itself and draws nothing; siteswap_grid.h draws it and
// turns keys and clicks into these edits.
#pragma once

#include "siteswap.h"

#include <string>
#include <vector>

// One throw as written in a cell: "3", "4p", "3p2", "3p+1". A blank cell (value kOpenThrow) is a
// throw not decided yet ("?" in siteswap text).
struct GridCell {
    int value = kOpenThrow;
    bool pass = false;
    int target = 0;         // an absolute target as written ("3p2": 2); 0: none
    bool relative = false;  // a relative target ("3p+1", "3p-1")...
    int along = 0;          // ...how many jugglers along (negative for "-")

    bool blank() const { return value == kOpenThrow; }
    // As written ("4p+1"); "" for a blank.
    std::string text() const;
    bool operator==(const GridCell& o) const {
        return value == o.value && pass == o.pass && target == o.target && relative == o.relative && along == o.along;
    }
    bool operator!=(const GridCell& o) const { return !(*this == o); }
};

GridCell selfCell(int value);
// The character for a throw value (0-9, a-z), or 0 if siteswap can't write it (25 and 33 are
// p and x; above 35).
char throwChar(int value);
// A throw value from its character (case ignored), or -1.
int throwValue(char c);

struct GridRow {
    std::vector<GridCell> cells;  // (none for a link)
    std::string link;             // "@2[3]" as written (without spaces), or "" for throws
    std::string options;          // ",LRswap" etc., as written
    bool linked() const { return !link.empty(); }
};

struct SiteswapGrid {
    bool passing = false;  // written "<...|...>"
    int period = 0;        // columns (every row with throws has this many)
    std::vector<GridRow> rows;
    int jugglers() const { return static_cast<int>(rows.size()); }
};

// Reads siteswap text into a grid. Rows written shorter than the longest are filled out with
// blanks. With two jugglers a digit after "p" is the next throw ("3p3" is 3p, 3), as siteswap
// reads it. False if the text isn't siteswap (or is sync or multiplex).
bool gridFromText(const std::string& text, SiteswapGrid* grid);
// The grid as siteswap text: "531", "<3p 3|3p 3>", "<4p 3|@1[1],LRswap>"; blanks as "?".
std::string gridToText(const SiteswapGrid& grid);

// A count directive ("4-count"): only every n-th beat (beats 1, 5, 9, ...) holds anything of
// interest; the beats between hold the filler (3s), greyed out and locked.
struct GridCount {
    int n = 0;  // 0: none
    int filler = 3;
    bool active() const { return n >= 2; }
    bool countBeat(int beat) const { return !active() || beat % n == 0; }
};

// A beat between the count's beats that holds the filler: greyed out and locked.
bool isFillerCell(const SiteswapGrid& grid, int row, int beat, const GridCount& count);
// Where a cell's throw is written: the cell itself, or, in a linked row ("@2[3]"), the cell it
// copies (links followed: J3's beat b is J2's beat b + 3, taken round the period). False if a
// link can't be followed.
bool gridCellSource(const SiteswapGrid& grid, int row, int beat, int* sourceRow, int* sourceBeat);
// A cell the user can change: not in a linked row, not a filler cell.
bool isEditableCell(const SiteswapGrid& grid, int row, int beat, const GridCount& count);
// Whether the grid already is an n-count: its period a multiple of n, and the filler on every
// beat between.
bool gridFitsCount(const SiteswapGrid& grid, int n);
// Switching to an n-count by filling in: the beats that matter (the old count's beats, and any
// beat that isn't all filler) go to beats 1, n+1, 2n+1, ..., and the filler everywhere else.
SiteswapGrid gridWithCountFilledIn(const SiteswapGrid& grid, const GridCount& from, int n);
// Switching to an n-count keeping every throw where it is: the period is rounded up to a
// multiple of n (new beats blank on the count, filler between).
SiteswapGrid gridWithCountKept(const SiteswapGrid& grid, int n);
// Whether an edit made elsewhere (the ladder) keeps to the count: beats added or taken away n at a
// time, and no filler beat changed. False with the reason.
bool countAllowsChange(const SiteswapGrid& before, const SiteswapGrid& after, const GridCount& count,
                       std::string* why);
// The pattern, written for the count: each period of n beats holds one count beat.
int countPeriodFor(int shortestPeriod, const GridCount& count);

// Columns: inserted blank (or, `wholeCount`, one count's worth: blank on its count beat, filler
// between), or deleted. Links aren't touched.
void insertColumns(SiteswapGrid* grid, int at, int columns, const GridCount& count, bool wholeCount);
bool deleteColumns(SiteswapGrid* grid, int at, int columns, std::string* why);
// The shortest period the grid repeats at (every juggler with throws written out: their beats
// repeat that often), a multiple of the count with one; the period itself if it doesn't repeat.
int shortestGridPeriod(const SiteswapGrid& grid, const GridCount& count);
// Makes the grid at least this big: new beats and jugglers blank (on the count) or filler.
void extendGrid(SiteswapGrid* grid, int period, int jugglers, const GridCount& count);

// Jugglers: one added (before row `at`), or one taken away. Links and absolute pass targets to
// the jugglers after it are renumbered.
bool addJuggler(SiteswapGrid* grid, int at, const GridCount& count, std::string* why);
bool removeJuggler(SiteswapGrid* grid, int row, std::string* why);
void toggleLrSwap(SiteswapGrid* grid, int row);

// A throw one higher or lower (Ctrl+Up/Down): blank, 0, 1, ... 35, skipping 25 and 33. A 0 or a
// blank isn't a pass. False if it can't go further.
bool bumpCell(GridCell* cell, int delta);
// Where a throw goes, one juggler along (Ctrl+Shift+Up/Down): self, then a pass to each juggler
// in turn. Written relative if the grid has relative targets. False with the reason.
bool stepCellTarget(const SiteswapGrid& grid, int row, int beat, int delta, GridCell* out, std::string* why);
// Swaps where two throws in one row land: the throw at `first` and the one d beats later at
// `second` (which may have wrapped round the period). False with the reason.
bool swapCells(SiteswapGrid* grid, int row, int first, int second, int d, std::string* why);
// The same pattern started `steps` beats later (every row together). False if nothing moves.
bool rotateGrid(SiteswapGrid* grid, int steps);

// A rectangle of cells as siteswap text (for copying): one row "5 3 1", several "<5 3|3 3>".
std::string cellsToText(const SiteswapGrid& grid, int row0, int row1, int beat0, int beat1);
