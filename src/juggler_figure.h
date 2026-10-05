// juggler_figure.h - the stick-figure juggler: pose data and drawing.
//
// Juggler-local coordinates, in meters: origin on the floor between the feet, +Y up,
// and the juggler FACES +Z. So the juggler's own right side is at -X and left side at +X.
#pragma once

#include "math3d.h"
#include "mesh.h"
#include "renderer.h"

struct JugglerPose {
    Vec3 head;
    float headRadius = 0.11f;
    Vec3 neckBase, waist;

    // "R"/"L" are the juggler's own right and left.
    Vec3 shoulderR, shoulderL;
    Vec3 elbowR, elbowL;
    Vec3 wristR, wristL;
    Vec3 palmNormalR, palmNormalL;  // direction the palm faces (up for a self-catch)

    Vec3 hipR, hipL;
    Vec3 kneeR, kneeL;
    Vec3 ankleR, ankleL;
};

// Standing, forearms horizontal and forward, palms up: the neutral juggling stance.
JugglerPose makeNeutralPose();

struct JugglerStyle {
    Vec3 bodyColor{0.78f, 0.80f, 0.86f};
    Vec3 handColor{0.95f, 0.78f, 0.62f};
    float limbRadius = 0.028f;
    bool drawLegs = true;
};

// placement maps juggler-local space into the world (position + facing).
void drawJuggler(Renderer& renderer, const Primitives& prims, const JugglerPose& pose,
                 const Mat4& placement, const JugglerStyle& style);
