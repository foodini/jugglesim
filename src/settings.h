// settings.h - per-user application preferences (not part of any pattern file).
//
// Stored as simple "key = value" lines in %APPDATA%\JuggleSim\settings.ini. Unknown keys are
// ignored and missing keys keep their defaults, so the file can grow without versioning.
#pragma once

#include "color_vision.h"
#include "juggle_sim.h"
#include "response_curve.h"

#include <filesystem>
#include <string>

enum class FloorGridType { None, Square, Radial, Triangles };

struct AppSettings {
    ColorVisionMode colorVision = ColorVisionMode::Normal;
    PropType prop = PropType::Ball;
    bool tempoPanelCollapsed = false;  // the juggler pane's Tweakables panel
    bool showThrowValues = false;      // the ladder's throw-value labels
    bool colorByOrbit = false;         // color props by orbit instead of one color each
    bool dimLinked = false;            // mute linked jugglers' throws on the ladder
    bool pathBeatNumbers = true;       // choreography: number every beat along the paths
    bool sounds = true;                // the plip when something is refused (see sound.h)
    bool metronome = false;            // a tick on every beat while playing (A)
    bool showKeyframeMarks = false;    // the ladder's keyframes labeled with their spike mark (K)
    // Choreography: the floor grid spike marks (and keyframe spots) snap to: the one in use, which
    // the choreography also keeps (Choreography::grid), so a pattern brings its own back.
    // Changing it never moves anything.
    FloorGridType floorGrid = FloorGridType::None;
    float gridSpacing = 0.5f;          // m between lines (square, triangles) or rings (radial), 0.25..2
    int gridSpokes = 8;                // radial: spokes, 3..32
    int gridMajorEvery = 4;            // every so many lines (rings) drawn mid-weight, 2..12
    int gridSpokeMajor = 0;            // radial: every so many spokes mid-weight (if it divides them); 0: none
    bool gridHalfTurn = false;         // radial: turned half a spoke (else a spoke points at the camera)
    bool choreographyPanelCollapsed = false;
    float slowRate = 0.25f;            // slow-motion playback speed, 0.05..0.5 of normal
    float ladderFraction = 0.5f;       // the ladder pane's share of the window width
    // A 3D mouse's response (see response_curve.h; edited in Edit > Preferences): sliding, in
    // pixels a second on the screen at the orbit point, and turning, in degrees a second, by how
    // far each axis is pushed as a percentage of its full push...
    ResponseCurve spaceSlideCurve = defaultSlideCurve();
    ResponseCurve spaceTurnCurve = defaultTurnCurve();
    // ...a hard push, as it's normally pushed, on each axis (raw counts; Preferences' Calibrate):
    // slides (x, y, z), then turns (tilt, roll, spin), as in SpaceMouseState. (A cap can reach
    // much further on some axes than others.)
    float spaceFullPush[6] = {7300.0f, 1600.0f, 9100.0f, 800.0f, 450.0f, 500.0f};
};

// Returns defaults for anything missing or unreadable.
AppSettings loadSettings();
// ...the same, from a settings file's text.
AppSettings settingsFromText(const std::string& fileText);

// Returns false if the file couldn't be written (the app keeps running either way).
bool saveSettings(const AppSettings& settings);
// ...the file's text.
std::string settingsText(const AppSettings& settings);

// Where per-user files live: %APPDATA%\JuggleSim\<fileName> on Windows,
// ~/Library/Application Support/JuggleSim/<fileName> on macOS, or the working directory if
// that can't be found. (The folder may not exist yet; writers create it.)
std::filesystem::path userDataPath(const wchar_t* fileName);
