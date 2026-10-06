// draw_helpers.h - ImGui draw-list helpers: marker shapes, dashed curves, arrowheads.
#pragma once

#include "color_vision.h"
#include "imgui.h"

// Filled marker centered at `center`. If outlineWidth > 0, it is first drawn enlarged in
// outlineColor, giving a ring that separates the marker from lines passing underneath it.
void drawMarker(ImDrawList* dl, ImVec2 center, float radius, MarkerShape shape, ImU32 color,
                float outlineWidth = 0.0f, ImU32 outlineColor = 0);

// Cubic Bezier drawn with a dash pattern. dashUnit scales the pattern (pixels per unit).
void drawStyledBezier(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, ImU32 color,
                      float thickness, DashPattern dash, float dashUnit);

// Straight line with a dash pattern.
void drawStyledLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float thickness,
                    DashPattern dash, float dashUnit);

// Point on a cubic Bezier at parameter t in [0, 1].
ImVec2 bezierPoint(ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, float t);

// Distance from `point` to the curve (sampled), and the curve parameter of the nearest point.
float bezierDistance(ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, ImVec2 point, float* tNearest);

// Blend color a toward b by t (0 = a, 1 = b), with the given output alpha (0-1).
ImU32 mixColor(ImU32 a, ImU32 b, float t, float alpha);

// Hover highlight for one half of a throw curve: a glow that is strongest at the chosen end
// (the departure if departureHalf, else the arrival) and fades out by the midpoint.
// intensity scales the whole effect (1 = full, smaller for repeats in Pattern mode).
void drawBezierHalfHighlight(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1,
                             bool departureHalf, ImU32 color, float intensity);

// The "picked up" look: a pulsing glow along the whole curve with sparks travelling along it.
// time is in seconds (drives the animation); intensity as above.
void drawHeldThrowEffects(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1,
                          ImU32 color, float time, float intensity);

// Filled arrowhead with its tip at `tip`, pointing away from `from`.
void drawArrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 from, ImU32 color, float size);

// Arrowhead for the end of a cubic Bezier, with its tip pulled back `inset` pixels along the
// curve from p1 (so it isn't hidden under a marker drawn at p1), aligned with the curve there.
void drawBezierArrowHead(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, ImU32 color,
                         float size, float inset);
