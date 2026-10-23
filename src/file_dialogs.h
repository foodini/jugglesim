// file_dialogs.h - the File menu's dialogs: Save Changes? (before a new pattern, or quitting),
// New Pattern, and the message when a file couldn't be written. Each keeps its own state (below)
// and says what was chosen; the caller does it (in app.cpp). Each opens itself when its state
// says it's requested.
#pragma once

#include "imgui.h"

#include <string>

// Save changes? Asked before starting a new pattern, or quitting, when the pattern has changed.
struct SavePrompt {
    bool requested = false;  // open it this frame
    bool forQuit = false;    // it's for quitting (not for File > New Pattern...)
    bool open = false;       // (as of the last frame drawn)
};
enum class SaveChangesChoice { None, Save, DontSave };  // (Cancel: None)
// `name`: what's showing (the pattern's name in quotes, or its siteswap); `also`: what else has
// changed (" (and its choreography)"), or ""; `patternIsOpen`: it's one of My Patterns (Save
// saves over it; otherwise Save... asks for a name).
SaveChangesChoice drawSaveChangesDialog(SavePrompt& p, const std::string& name, const char* also, bool patternIsOpen);

// File > New Pattern...: a blank sketch (every throw "?") for some jugglers and period.
struct NewPatternDialog {
    bool openRequested = false;
    int jugglers = 2;
    int period = 6;
    int count = 0;  // a count directive (0: none): blanks only on its beats, 3s between
    int walking = 0;  // the choreography's length, its own period (0: no choreography yet)
};

// Returns true when Create is chosen (make the blank pattern d asks for).
bool drawNewPatternDialog(NewPatternDialog& d);

// While `error` isn't empty: it, in a message box; OK clears it.
void drawFileErrorDialog(std::string& error);
