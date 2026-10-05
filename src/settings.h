// settings.h - per-user application preferences (not part of any pattern file).
//
// Stored as simple "key = value" lines in %APPDATA%\JuggleSim\settings.ini. Unknown keys are
// ignored and missing keys keep their defaults, so the file can grow without versioning.
#pragma once

#include "color_vision.h"

struct AppSettings {
    ColorVisionMode colorVision = ColorVisionMode::Normal;
};

// Returns defaults for anything missing or unreadable.
AppSettings loadSettings();

// Returns false if the file couldn't be written (the app keeps running either way).
bool saveSettings(const AppSettings& settings);
