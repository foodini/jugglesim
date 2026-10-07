// settings.h - per-user application preferences (not part of any pattern file).
//
// Stored as simple "key = value" lines in %APPDATA%\JuggleSim\settings.ini. Unknown keys are
// ignored and missing keys keep their defaults, so the file can grow without versioning.
#pragma once

#include "color_vision.h"
#include "juggle_sim.h"

#include <filesystem>

struct AppSettings {
    ColorVisionMode colorVision = ColorVisionMode::Normal;
    PropType prop = PropType::Ball;
    bool tempoPanelCollapsed = false;  // the juggler pane's Tweakables panel
    bool showThrowValues = false;      // the ladder's throw-value labels
    bool colorByOrbit = false;         // color props by orbit instead of one color each
    float ladderFraction = 0.5f;       // the ladder pane's share of the window width
};

// Returns defaults for anything missing or unreadable.
AppSettings loadSettings();

// Returns false if the file couldn't be written (the app keeps running either way).
bool saveSettings(const AppSettings& settings);

// Where per-user files live: %APPDATA%\JuggleSim\<fileName> on Windows,
// ~/Library/Application Support/JuggleSim/<fileName> on macOS, or the working directory if
// that can't be found. (The folder may not exist yet; writers create it.)
std::filesystem::path userDataPath(const wchar_t* fileName);
