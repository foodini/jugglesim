// color_vision.h - color-vision modes and the per-ball visual identity (color + shape + dash).
//
// Identity never relies on color alone: every ball gets a marker shape and a dash pattern as
// well, so balls stay distinguishable in any mode, including Monochrome. The palettes were
// chosen by optimizing worst-case pairwise OKLab distance under the Machado (2009) color-vision
// simulation for each mode, against the dark canvas color below. Palette order matters: the
// first k colors of a palette are the best-separated set of k, so balls 0..k-1 are always as
// distinct as possible.
#pragma once

#include "imgui.h"

enum class ColorVisionMode { Normal, Protan, Deutan, Tritan, Monochrome, Count };

enum class MarkerShape { Circle, Square, TriangleUp, Diamond, TriangleDown, Hexagon, Count };

enum class DashPattern { Solid, LongDash, ShortDash, DashDot, Count };

struct BallStyle {
    ImU32 color;
    MarkerShape shape;
    DashPattern dash;
};

// Background of the ladder and other diagram canvases (the palettes are validated against it).
constexpr ImU32 kCanvasBackground = IM_COL32(14, 15, 19, 255);

// Human-readable name for menus, e.g. "Deutan (green-weak)".
const char* colorVisionModeName(ColorVisionMode mode);

// Stable token for the settings file, e.g. "deutan".
const char* colorVisionModeKey(ColorVisionMode mode);
bool colorVisionModeFromKey(const char* key, ColorVisionMode* mode);

BallStyle ballStyle(ColorVisionMode mode, int ballIndex);

// A dash pattern as alternating on/off lengths, in "dash units" (the caller picks the unit:
// pixels on the ladder, meters for 3D trails). count is 0 for a solid line.
void dashPatternLengths(DashPattern dash, const float** lengths, int* count);

// Color for error text. Error text always says what is wrong in words; this is just emphasis.
ImU32 errorTextColor(ColorVisionMode mode);

// A window showing every mode's ball styles side by side, for checking which palette reads
// best. Pass the open flag from the View menu.
void drawColorVisionPreview(bool* open, ColorVisionMode current);
