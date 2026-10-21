// app_widgets.h - small pieces of the app's UI: the transport buttons, the siteswap grid's throw
// descriptions, and the keyboard shortcuts window.
#pragma once

#include "siteswap.h"
#include "imgui.h"

#include <string>

// The transport icons, drawn into a button's rectangle.
enum class TransportIcon { ToStart, StepBack, Play, Pause, StepForward };

// A button with a transport icon on it; true when clicked.
bool transportButton(const char* id, TransportIcon icon, float size);

// A throw described for the siteswap grid's tooltip: "J1, beat 3, right hand: 4p, a pass to J2's
// left hand, caught on beat 7." Beats count from 1, as on the ladder; each juggler's first
// throw is from the right hand (the left, if their hands are swapped).
std::string describeThrow(const JugglingLoop& loop, int juggler, int beat);

// The keyboard shortcuts sheet (? or Help > Keyboard Shortcuts): every key and mouse action,
// by where it works.
void drawShortcutsWindow(bool* open);
