// siteswap_edit.h - help for typing siteswap in the text box: autofill, changing a throw with
// Up/Down, swapping two throws' landings, tidying, and finding which throw a character is in.
//
// Everything here works on the text itself (with a cursor and selection, as character offsets)
// and draws nothing; app.cpp wires it to the text box.
#pragma once

#include <string>

// What the text box shows when the user has just typed something that calls for more: the
// text and where the cursor should go.
struct SiteswapEdit {
    std::string text;
    int cursor = 0;
};

// Autofill after an edit took the text from `before` to `after` (cursor in `after`). Returns
// true, with the filled-in text, when the edit calls for it:
//   - "<" typed in an empty box gives "<|>"; "<" typed before a solo pattern ("531") makes it a
//     passing pattern with a second juggler still to decide ("<5 3 1|? ? ?>").
//   - A "|" typed at the end of a passing pattern adds a juggler, all "?".
//   - A throw inserted into one juggler's part puts a "?" at the same position in the others
//     (or, if a part was shorter, pads it with "?" at the end).
//   - A real throw deleted from one juggler's part deletes the "?"s at the same position in the
//     others. Deleting a "?" deletes just that "?": it never touches other jugglers' parts.
//   - A pass to a juggler the pattern doesn't have yet ("3p+2" with two jugglers) adds jugglers,
//     all "?" (up to kMaxJugglers).
// Passing patterns are rewritten in the standard style: spaces between throws, none around the
// bars. A missing ">" is added.
//
// Deleting a real throw deletes the "?"s across from it, but backspacing a throw is also how
// you'd start replacing it. So such a column deletion is remembered for one step: if the very
// next edit types a throw at the same spot, it was a replacement after all, and what the
// deletion took from the other parts comes back ("<7 5 3 1|? 5 3 1>", backspace the 7, type 8:
// "<8 5 3 1|? 5 3 1>"). Anything else (another edit, moving the cursor, leaving the box)
// forgets it; the caller calls forget() for the last two.
struct SiteswapAutofillMemory {
    bool armed = false;
    std::string text;          // the text just after the column was deleted
    int cursor = 0;            // where the cursor was left
    std::string beforeDelete;  // the text before it
    int part = 0;              // the part the user deleted from, and the column
    int column = 0;
    void forget() { armed = false; }
};

bool autofillSiteswap(const std::string& before, const std::string& after, int cursor, SiteswapEdit* result,
                      SiteswapAutofillMemory* memory = nullptr);

// Changes the throw at the cursor (the one it's in or just after) by delta (+1 or -1): values
// run ? (not decided), 0, 1, ... 35, skipping 25 and 33 (whose letters, p and x, mean passing and
// sync). A 0 can't be a pass, so it loses its "p". Returns false if there's no throw there or
// it can't go further.
bool bumpThrowAtCursor(const std::string& text, int cursor, int delta, SiteswapEdit* result);

// Changes where the throw at the cursor goes (Shift+Up/Down), by delta jugglers along: a self
// becomes a pass to the next juggler (delta +1) or the one before (-1), and stepping on past the
// last juggler comes back to a self. Written in the pattern's style: relative ("3p+1") if the
// text already uses relative targets, else as Juggling Lab writes it ("3p2", or a bare "p" with
// two jugglers). False (with *why) for a solo pattern, a 0 or a "?".
bool stepPassTarget(const std::string& text, int cursor, int delta, SiteswapEdit* result, std::string* why);

// Swaps where two throws in the same juggler's part land (a "siteswap"): the two selected
// throws, or with no selection, the two throws just before the cursor. Throws at positions i < j
// (d = j - i apart) become: position i gets throw j's value + d (and its destination), position
// j gets throw i's value - d. "531" with "31" selected becomes "522". Returns false, with the
// reason in *why, if that's not possible.
bool swapThrows(const std::string& text, int selectionStart, int selectionEnd, int cursor, SiteswapEdit* result,
                std::string* why);

// Rotates the pattern: steps > 0 moves throws right, the last one coming round to the front
// (7531 becomes 1753), steps < 0 left. Every juggler's part rotates together, so it's the same
// pattern started on a different beat. The text comes back in standard form, with the cursor
// after the same number of throws in its part. False (with *why) if there's nothing to rotate.
bool rotateThrows(const std::string& text, int steps, int cursor, SiteswapEdit* result, std::string* why);

// The text in standard form (lower case; for passing, spaces between throws and none around the
// bars, with the closing ">"). Text that isn't siteswap is returned unchanged.
std::string tidySiteswap(const std::string& text);

// Which throw the character at `index` belongs to: the juggler (0 for a solo pattern) and its
// position in that juggler's part (the beat in the loop). False if it isn't part of a throw.
bool throwAtCharacter(const std::string& text, int index, int* juggler, int* beat);

// If the character at `index` is in a link ("@2[3]"), the juggler whose part it is, the juggler
// they copy (0-based), where in that juggler's throws they start (as written: may be negative or
// past the period), and whether their hands are swapped (",LRswap").
bool linkAtCharacter(const std::string& text, int index, int* juggler, int* to, int* start, bool* lrSwap);

// The characters [*start, *end) of a throw, by juggler and beat in the loop. For a linked
// juggler, the characters of the throw they copy.
bool charactersOfThrow(const std::string& text, int juggler, int beat, int* start, int* end);

// The throw at the cursor (the one it's in or just after, else the one starting there): the
// juggler (0 for a solo pattern) and its position in their part, which is its beat in the loop.
// False if the cursor isn't at a throw (in a link, say).
bool throwAtCursor(const std::string& text, int cursor, int* juggler, int* beat);

// Ctrl+click in the box: makes the part the cursor is in a link to the juggler whose throw was
// clicked (clickIndex is a character of that throw), starting with that throw: "@k[i]", where i
// is the throw's position in their part (counting from 0). Clicking J1's beat 9 gives "@1[8]":
// on beat 1 the copy throws J1's beat 9. Whatever the part held is replaced; its options
// (",LRswap") are kept. A start of 0 is written "@k". False (with *why) if the click wasn't on a
// throw in another juggler's part.
bool linkToClickedThrow(const std::string& text, int cursor, int clickIndex, SiteswapEdit* result, std::string* why);
