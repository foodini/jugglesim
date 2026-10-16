// siteswap_grid.h - the siteswap grid: the pattern as a spreadsheet of throws, one row per
// juggler and one column per beat, along the bottom of the window.
//
// Typing a throw replaces the cell's throw. A cell finishes as soon as the next key can't be part
// of it ("5" then "3": the 3 goes in the next cell; "5" then "p": a pass), or when you move away;
// a pass finishes with its target (with two jugglers, as soon as the "p" is typed). Backspace and
// Delete clear cells (a blank cell: a throw not decided yet). With a count directive ("4-count")
// only beats 1, 5, 9, ... are edited; the 3s between are greyed out and skipped.
//
// A linked juggler's row ("@1[3]") shows the throws they copy, and typing there edits the throw
// copied: it and every other copy of it change together (outlined while the cursor is on one).
// A target typed there is as that juggler sees it: + or - along, never a juggler's number.
//
// With two or more jugglers the rows are positions, not people: the throws' rows are T1, T2, ...
// (whose throws) and the choreography's C1, C2, ... (whose path). Without a loop, T2 and C2 are
// J2; the next columns (beside the row names) hand the positions round, and a row's name then
// says who's on it at the playhead when that's someone else ("T2 J3").
//
// The grid is drawn and polled each frame: it reads the siteswap text and hands back what to
// change (new text, an undo step, a count, a link), and the app applies it.
#pragma once

#include "color_vision.h"
#include "siteswap.h"
#include "siteswap_edit.h"

#include <string>
#include <utility>
#include <vector>

// The choreography's rows, under the throws' (one per juggler, in the same beat columns): which
// spike mark each juggler arrives on, beat by beat. The app makes this from the choreography
// (choreography.h) each frame; the grid draws it and hands back edits (SiteswapGridResult).
struct ChoreoGridCell {
    std::string text;      // the mark they arrive on ("05"; "05~": turning the long way round), or ""
    bool stay = false;     // (no keyframe) still standing on the mark before: a loiter, drawn as a bar
    bool locked = false;   // a copy (a walk link's, or the walking loop's): dim, not edited here
};
struct ChoreoGridRow {
    std::string link;      // a walk link, as the row's header says it ("@1[16] -1/6"), or ""
    std::vector<ChoreoGridCell> cells;  // one per beat of the whole choreography (repeats and all)
    // The next column (beside the row's name): which C row whoever's on this one goes on to after
    // the beats written out, and how far turned ("2r+1/4"); "" for its own, unturned (shown dim).
    // loopBad: it's part of a problem.
    std::string loop;
    bool loopBad = false;
};
struct ChoreoGridView {
    bool active = false;
    int written = 0;       // beats written out (Length); the rest are the walking loop's copies
    int full = 0;          // beats in all
    std::vector<ChoreoGridRow> rows;
    std::vector<std::string> markNames;
    std::string loopProblem;  // what's wrong with the loop column, or ""
    // Who's on each row (C1, C2, ...) at the playhead, when it isn't their own juggler (-1: it is).
    std::vector<int> who;
    bool looping = false;     // the walking loop hands rows round (so a row can show someone else)
};
struct ChoreoEdit {
    // Set, Clear, LongTurn: a keyframe; InsertBeat, DeleteBeat: a beat of the walking (everyone's);
    // Loop: a juggler's loop column.
    enum class Kind { Set, Clear, LongTurn, InsertBeat, DeleteBeat, Loop } kind = Kind::Set;
    int juggler = 0, beat = 0;
    std::string mark;      // Set: the mark's name; Loop: what was typed ("2r+1/4"; "": their own)
};

// The throwing loop (passing patterns): a next column (beside the row names) saying which T row
// whoever is on each one goes on to after the period; and, past the period, how long the loop
// takes to come round (its clashes shown on the rows they're in). The app makes this each frame
// (see playedLoop in loop_ops.h); the grid draws it and hands back edits.
struct ThrowLoopGridView {
    bool active = false;
    std::vector<std::string> next;   // per row: "2", or "" (its own: shown dim)
    std::vector<char> bad;           // ...that row's next is part of a problem (red)
    JugglingLoop played;             // the loop played out (by juggler), if it differs from the written one
    std::vector<std::vector<int>> part;  // [phase][juggler]: which row they're on (phase: beat / period)
    std::vector<std::pair<int, int>> clash;  // {juggler, beat of played}: throws that don't work
    // Who's on each row (T1, T2, ...) at the playhead, when it isn't their own juggler (-1: it is).
    std::vector<int> who;
    bool looping = false;            // the throwing loop hands rows round (so a row can show someone else)
};

struct SiteswapGridState {
    bool focused = false;       // the grid has the keyboard
    int row = 0, beat = 0;      // the cursor's cell
    int anchorRow = 0, anchorBeat = 0;  // the selection's other corner (the cursor's cell: none)
    bool editing = false;       // a throw is being typed into the cursor's cell
    std::string editText;       // ...what's been typed
    GridCell editOriginal;      // ...and what the cell held before
    std::string lastText;       // the text the grid last saw or made
    bool selecting = false;     // dragging out a selection
    bool scrollToCursor = false;
    // Menus and dialogs.
    int menuRow = 0, menuBeat = 0;
    int countWanted = -1;       // the count chosen, while asking how to get there
    bool askCount = false;
    struct Paste {
        bool active = false;
        SiteswapGrid clip;
        int row = 0, beat = 0;
        int step = 1;           // 1: beats in a row; n: onto the count's beats
    } paste;
    bool askPasteWhere = false, askPasteExtend = false;
    // The cursor in the choreography's rows (instead of the throws').
    bool inChoreo = false;
    int choreoRow = 0, choreoBeat = 0;
    std::string choreoTyped;    // a mark's name being typed
    bool inLoop = false;        // ...the cursor's in the loop column (choreoBeat is then ignored)
    int choreoMenuRow = 0;
    int choreoMenuBeat = -1;    // the beat its menu was opened on (-1: the row's header)
    // The cursor in the throws' next column (row: whose).
    bool inThrowNext = false;
    int throwNextMenuRow = 0;
};

struct SiteswapGridResult {
    bool changed = false;     // show `text`
    std::string text;
    bool commit = false;      // ...after recording `commitText` as an undo step
    std::string commitText;
    int newCount = -1;        // the count directive changed (0: none), with `text` if changed
    bool restart = false;     // a whole pattern was pasted: play it from beat 1
    std::string note;         // something that couldn't be done, and why
    int hoverJuggler = -1, hoverBeat = -1;  // the cell under the mouse (its throw's beat in the period)
    int hoverColumn = -1;                   // ...and its column (past the period: a copy)
    int linkJuggler = -1, linkTo = -1, linkStart = 0;  // make linkJuggler a copy of linkTo
    int unlinkJuggler = -1;   // write a linked juggler's throws out
    std::string readout;      // the cursor's cell: "J1, beat 5, right hand: 3p+2"
    bool plip = false;        // something just typed or done was refused (see `note`)
    // The choreography's rows.
    std::vector<ChoreoEdit> choreoEdits;
    double scrubBeat = -1.0;  // go to this beat (paused): the cursor moved to it
    int selectJuggler = -1;   // ...with this juggler selected
    int walkLinkJuggler = -1, walkLinkLeader = -1, walkLinkOffset = 0;  // a walk link (Ctrl+click)
    int walkUnlinkJuggler = -1;  // write a walk link out as keyframes
    int walkLikeJuggler = -1;    // open the Walk Like... window
    int loopBuilderJuggler = -1; // open the loop column's builder for this juggler
    // The throws' next column: {juggler, whose throws they take up next (-1: their own)}.
    std::vector<std::pair<int, int>> throwNextEdits;
};

// The height the grid needs for this many jugglers, and choreography rows (with its line of
// buttons under it).
float siteswapGridHeight(int jugglers, int choreoRows = 0);

// Draws the grid at the cursor, full width, and handles the mouse and (while it has the
// keyboard) the keys. `count`: the count directive (0: none). Leaves the cursor on the grid's
// last line, after its Count button, for a status message.
SiteswapGridResult drawSiteswapGrid(SiteswapGridState& state, const std::string& text, const Siteswap& parsed,
                                    int count, ColorVisionMode colorVision, double playBeat,
                                    const ChoreoGridView& choreo = ChoreoGridView(),
                                    const ThrowLoopGridView& throwLoop = ThrowLoopGridView());

// Whether the grid wants keys this frame: the app's single-key shortcuts stay off meanwhile.
inline bool siteswapGridHasKeyboard(const SiteswapGridState& state) { return state.focused; }
