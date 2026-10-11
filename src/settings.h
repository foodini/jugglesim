// settings.h - per-user application preferences (not part of any pattern file).
//
// Stored as simple "key = value" lines in %APPDATA%\JuggleSim\settings.ini. Unknown keys are
// ignored and missing keys keep their defaults, so the file can grow without versioning.
#pragma once

#include "color_vision.h"
#include "juggle_sim.h"

#include <filesystem>

enum class FloorGridType { None, Square, Radial, Triangles };

struct AppSettings {
    ColorVisionMode colorVision = ColorVisionMode::Normal;
    PropType prop = PropType::Ball;
    bool tempoPanelCollapsed = false;  // the juggler pane's Tweakables panel
    bool showThrowValues = false;      // the ladder's throw-value labels
    bool colorByOrbit = false;         // color props by orbit instead of one color each
    bool dimLinked = false;            // mute linked jugglers' throws on the ladder
    bool pathBeatNumbers = true;       // choreography: number every beat along the paths
    bool showKeyframeMarks = false;    // the ladder's keyframes labeled with their spike mark (K)
    // Choreography: the floor grid spike marks (and keyframe spots) snap to. Not part of any
    // pattern: changing it never moves anything.
    FloorGridType floorGrid = FloorGridType::None;
    float gridSpacing = 0.5f;          // m between lines (square, triangles) or rings (radial), 0.25..2
    int gridSpokes = 8;                // radial: spokes, 3..32
    int gridMajorEvery = 4;            // every so many lines (rings) drawn mid-weight, 2..12
    int gridSpokeMajor = 0;            // radial: every so many spokes mid-weight (if it divides them); 0: none
    bool gridHalfTurn = false;         // radial: turned half a spoke (else a spoke points at the camera)
    bool choreographyPanelCollapsed = false;
    float slowRate = 0.25f;            // slow-motion playback speed, 0.05..0.5 of normal
    float ladderFraction = 0.5f;       // the ladder pane's share of the window width
    float spaceMouseSpeed = 1.0f;      // how fast a 3D mouse moves the camera, 0.25..4
};

// Returns defaults for anything missing or unreadable.
AppSettings loadSettings();

// Returns false if the file couldn't be written (the app keeps running either way).
bool saveSettings(const AppSettings& settings);

// Where per-user files live: %APPDATA%\JuggleSim\<fileName> on Windows,
// ~/Library/Application Support/JuggleSim/<fileName> on macOS, or the working directory if
// that can't be found. (The folder may not exist yet; writers create it.)
std::filesystem::path userDataPath(const wchar_t* fileName);
