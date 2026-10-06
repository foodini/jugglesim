// juggle_sim.h - where everything is at a given moment: hands, balls and ball trails.
//
// Everything here is a pure function of time. Ball flights are closed-form parabolas solved
// from when and where each throw is made and caught, so any moment can be evaluated directly:
// playing, pausing, and stepping forwards or backwards are all just "evaluate at beat t".
//
// Timing (in beats; one beat = 60/bpm seconds):
//   - A throw of value v made on beat b is caught on beat b + v - d, where d is the dwell of
//     that catch: the user's dwell D, but at most half the throw (min(D, v/2)), so short throws
//     like 1s still get some flight. The ball is then held until it's thrown again on b + v.
//   - A 2 is a hold: the ball stays in the hand for the two beats.
//   - Each hand throws every other beat (right hand on even beats) and follows a fixed loop:
//     from its throw point (inside) out to its catch point (outside) while empty, then a
//     scooping carry back to the throw point while holding a ball.
//
// Juggler-local coordinates: meters, +Y up, the juggler faces +Z, their right hand is at -X.
#pragma once

#include "math3d.h"
#include "pattern.h"

#include <vector>

struct JuggleParams {
    double bpm = 150.0;
    double dwellBeats = 1.4;  // 0 < dwell < 2
};

constexpr float kBallRadius = 0.034f;  // a typical 68 mm juggling ball

struct BallState {
    int ball = 0;   // ball id (BallOrbits numbering, same as the ladder)
    Vec3 center;
    bool inFlight = false;
};

// A ball's recent path, oldest point first. fade[i] goes from 0 (the oldest end, about to
// disappear) to 1 (where the ball is now, or where it was caught).
struct Trail {
    int ball = 0;
    std::vector<Vec3> points;
    std::vector<float> fade;
};

struct JugglerScene {
    Vec3 palmRight, palmLeft;  // where each hand's palm is (the ball sits just above it)
    std::vector<BallState> balls;
    std::vector<Trail> trails;
};

// How much space the pattern uses (juggler-local), for framing the camera: the lowest point
// the hands reach, the highest point any ball reaches, and how far out to the side things go.
struct SceneExtents {
    float lowestHandY = 0.0f;    // bottom of the hands at the lowest point of their scoop
    float highestPropY = 0.0f;   // top of the highest ball at the top of its flight
    float halfWidth = 0.0f;      // largest |x| of hands or balls
};
SceneExtents computeSceneExtents(const std::vector<int>& loop, const JuggleParams& params);

// Evaluates the scene at `beat` (fractional; beat 0 is the right hand's first throw) for the
// repeating loop `loop` (Pattern-mode loop values). orbits must be computeBallOrbits(loop).
JugglerScene evaluateScene(const std::vector<int>& loop, const BallOrbits& orbits,
                           const JuggleParams& params, double beat);
