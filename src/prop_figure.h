// prop_figure.h - drawing the props (balls, clubs, rings, hoops) in the 3D view.
//
// Each prop is drawn in its ball's ladder color. Clubs have a darker top, collar and knob (in
// the same hue, kept bright enough to stand out against the background). Rings are banded in
// the same dash pattern as the ball's ladder line, so you can see them spin and tell them
// apart without relying on color; a ring whose line is solid gets two narrow dark bands.
// Hoops are banded the same way.
#pragma once

#include "color_vision.h"
#include "juggle_sim.h"
#include "mesh.h"
#include "renderer.h"

struct PropMeshes {
    // A club, along +Y with its center of mass at the origin (top at +Y). Split by color.
    Mesh clubCap, clubBody, clubCollar, clubHandle, clubKnob;
    // A torch (from a real one): wick, shaft, rubber cap and knob soot-black; the cone below
    // the cap and the handle in the prop's color; a black band of tape between them.
    Mesh torchWick, torchShaft, torchCap, torchCone, torchBand, torchHandle, torchKnob;
    // A ring in the XZ plane (normal +Y), centered at the origin, split into its colored and
    // dark bands, one pair per dash pattern.
    Mesh ringColored[static_cast<int>(DashPattern::Count)];
    Mesh ringDark[static_cast<int>(DashPattern::Count)];
    // A hoop the same way (a torus), banded like the rings.
    Mesh hoopColored[static_cast<int>(DashPattern::Count)];
    Mesh hoopDark[static_cast<int>(DashPattern::Count)];

    void create();
    void destroy();
};

// The darker trim color for a prop of the given color.
Vec3 propTrimColor(Vec3 color);

// Draws one prop. ballSphere is the unit sphere mesh (for balls).
void drawProp(Renderer& renderer, const PropMeshes& meshes, const Mesh& ballSphere,
              const BallState& prop, Vec3 color, DashPattern dash);
