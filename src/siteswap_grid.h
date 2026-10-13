// siteswap_grid.h - the siteswap grid: the pattern as a spreadsheet of throws, one row per
// juggler and one column per beat, along the bottom of the window.
//
// Typing a throw replaces the cell's throw. A cell finishes as soon as the next key can't be part
// of it ("5" then "3": the 3 goes in the next cell; "5" then "p": a pass), or when you move away;
// a pass finishes with its target (with two jugglers, as soon as the "p" is typed). Backspace and
// Delete clear cells (a blank cell: a throw not decided yet). With a count directive ("4-count")
// only beats 1, 5, 9, ... are edited; the 3s between are greyed out and skipped.
//
// The grid is drawn and polled each frame: it reads the siteswap text and hands back what to
// change (new text, an undo step, a count, a link), and the app applies it.
#pragma once

#include "color_vision.h"
#include "siteswap.h"
#include "siteswap_edit.h"

#include <string>

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
};

struct SiteswapGridResult {
    bool changed = false;     // show `text`
    std::string text;
    bool commit = false;      // ...after recording `commitText` as an undo step
    std::string commitText;
    int newCount = -1;        // the count directive changed (0: none), with `text` if changed
    bool restart = false;     // a whole pattern was pasted: play it from beat 1
    std::string note;         // something that couldn't be done, and why
    int hoverJuggler = -1, hoverBeat = -1;  // the cell under the mouse
    int linkJuggler = -1, linkTo = -1, linkStart = 0;  // make linkJuggler a copy of linkTo
    int unlinkJuggler = -1;   // write a linked juggler's throws out
    std::string readout;      // the cursor's cell: "J1, beat 5, right hand: 3p+2"
};

// The height the grid needs for this many jugglers (with its line of buttons under it).
float siteswapGridHeight(int jugglers);

// Draws the grid at the cursor, full width, and handles the mouse and (while it has the
// keyboard) the keys. `count`: the count directive (0: none). Leaves the cursor on the grid's
// last line, after its Count button, for a status message.
SiteswapGridResult drawSiteswapGrid(SiteswapGridState& state, const std::string& text, const Siteswap& parsed,
                                    int count, ColorVisionMode colorVision, double playBeat);

// Whether the grid wants keys this frame: the app's single-key shortcuts stay off meanwhile.
inline bool siteswapGridHasKeyboard(const SiteswapGridState& state) { return state.focused; }
