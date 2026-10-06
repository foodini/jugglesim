// juggler_figure.h - the stick-figure juggler: pose data and drawing.
//
// Juggler-local coordinates, in meters: origin on the floor between the feet, +Y up,
// and the juggler FACES +Z. So the juggler's own right side is at -X and left side at +X.
#pragma once

#include "math3d.h"
#include "mesh.h"
#include "juggle_sim.h"
#include "renderer.h"

struct JugglerPose {
    Vec3 head;
    float headRadius = 0.11f;
    Vec3 headForward{0.0f, 0.0f, 1.0f};  // where the face points (the eyes are drawn there)
    Vec3 neckBase, waist;

    // "R"/"L" are the juggler's own right and left.
    Vec3 shoulderR, shoulderL;
    Vec3 elbowR, elbowL;
    Vec3 wristR, wristL;
    Vec3 palmNormalR, palmNormalL;  // direction the palm faces (up for a self-catch)
    // Direction the fingers point, in the palm's plane. The hand is drawn from the wrist along
    // this, so the wrist bends to keep the palm facing palmNormal whatever the forearm does.
    Vec3 handForwardR{0.0f, 0.0f, 1.0f}, handForwardL{0.0f, 0.0f, 1.0f};

    Vec3 hipR, hipL;
    Vec3 kneeR, kneeL;
    Vec3 ankleR, ankleL;
};

// Standing, forearms horizontal and forward, palms up: the neutral juggling stance.
JugglerPose makeNeutralPose();

// Moves the body of a neutral pose: the upper body by the motion's pelvis offset, lean and
// roll; the legs bend (two-bone IK, knees forward) to keep the feet planted; the head turns
// to look at motion.lookAt, within what a neck can do.
void poseBody(JugglerPose& pose, const BodyMotion& motion);

// Moves the arms so each palm is at the given point (palms facing up), solving each arm as a
// two-bone chain (upper arm + forearm, lengths taken from the pose) with the elbow pointing
// down and out; elbowFlare (0..1) swings the elbows further out to the sides. Targets out of
// reach are clamped to full extension. Call after poseBody, which moves the shoulders.
void poseArmsForPalms(JugglerPose& pose, Vec3 palmRight, Vec3 palmLeft, float elbowFlare = 0.0f);

struct JugglerStyle {
    Vec3 bodyColor{0.78f, 0.80f, 0.86f};
    Vec3 handColor{0.95f, 0.78f, 0.62f};
    Vec3 eyeColor{0.10f, 0.11f, 0.14f};
    float limbRadius = 0.028f;
    bool drawLegs = true;
};

// placement maps juggler-local space into the world (position + facing).
void drawJuggler(Renderer& renderer, const Primitives& prims, const JugglerPose& pose,
                 const Mat4& placement, const JugglerStyle& style);
