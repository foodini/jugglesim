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
//   - Each hand throws every other beat (right hand on even beats) and follows a loop: from
//     its throw point (inside) out to its catch point (outside) while empty, then a scooping
//     carry back to the throw point while holding a ball. Higher throws are made and caught a
//     little wider.
//   - The body helps with high throws: a hand can only drive a throw so far with the arm, so
//     the rest comes from the knees and back (the hips sink during the scoop and rise through
//     the release), and the torso sways toward the hand that's driving. See BodyMotion.
//   - Passing: every juggler throws on every beat, right hand on even beats. A pass flies from
//     one juggler's hand to another's; balls are caught where selfs are, clubs and rings a
//     little outside the shoulder and in front of the body, higher for higher passes.
//
// Juggler-local coordinates: meters, +Y up, the juggler faces +Z, their right hand is at -X.
// Each juggler has their own, placed in the world by jugglerPlacement(). Hands, bodies and
// poses are worked out in juggler-local coordinates; flying props in world coordinates.
#pragma once

#include "math3d.h"
#include "pattern.h"

#include <vector>

// What's being juggled. (Per pattern for now; BallState carries it per prop, so mixed props
// can come later.)
enum class PropType { Ball, Club, Ring, Count };

const char* propTypeName(PropType prop);  // "Balls", "Clubs", "Rings" (menu text)
const char* propTypeKey(PropType prop);   // "balls", ... (settings file)
bool propTypeFromKey(const char* key, PropType* prop);

struct JuggleParams {
    double bpm = 150.0;
    double dwellBeats = 1.4;  // 0 < dwell < 2
    PropType prop = PropType::Ball;
    double distance = 0.0;    // between passing jugglers (m, body to body); 0 = automatic
};

// Passing distances (m, body to body).
constexpr double kMinPassingDistance = 1.0, kMaxPassingDistance = 5.0;
// The automatic distance for a passing loop: grows with the highest throw (about 1.8 m for a
// pattern of 3s, 2.2 m with 4s, ...), within kMin/kMaxPassingDistance.
double defaultPassingDistance(const JugglingLoop& loop);
// The distance in use: params.distance, or the automatic one.
double passingDistance(const JugglingLoop& loop, const JuggleParams& params);

// Where juggler `juggler` stands and which way they face (yaw in radians about +Y: 0 faces +Z,
// pi/2 faces +X). One juggler stands at the origin facing +Z; two face each other along X,
// juggler 1 on the left (-X) as seen from the default camera.
void jugglerPlacement(const JugglingLoop& loop, const JuggleParams& params, int juggler,
                      Vec3* position, float* yaw);

constexpr float kBallRadius = 0.034f;  // a typical 68 mm juggling ball

// Club dimensions (a 515 mm club, 220 g), measured from the top (the big end) along its axis.
constexpr float kClubLength = 0.515f;
constexpr float kClubCenterOfMass = 0.215f;  // from the top
constexpr float kClubGrip = 0.387f;          // from the top: midway between collar and knob
// Ring dimensions: a typical stage ring.
constexpr float kRingOuterRadius = 0.16f;
constexpr float kRingInnerRadius = 0.125f;
constexpr float kRingHalfThickness = 0.003f;

// One prop at a moment. `center` is the center of mass (what flies on the parabola and what
// the trails follow). Clubs and rings also have an orientation:
//   - axis: for a club, along the club from the handle toward the top; for a ring, from the
//     point on the rim the hand holds toward the center (it turns as the ring spins).
//   - spinAxis: what it spins around (perpendicular to axis). For a ring, the ring's normal.
struct BallState {
    int ball = 0;   // ball id (BallOrbits numbering, same as the ladder)
    PropType prop = PropType::Ball;
    Vec3 center;
    Vec3 axis{0.0f, 1.0f, 0.0f};
    Vec3 spinAxis{1.0f, 0.0f, 0.0f};
    bool inFlight = false;
};

// A ball's recent path, oldest point first. fade[i] goes from 0 (the oldest end, about to
// disappear) to 1 (where the ball is now, or where it was caught).
struct Trail {
    int ball = 0;
    std::vector<Vec3> points;
    std::vector<float> fade;
};

// The juggler's body at a moment. The upper body (waist and everything above it) is the
// neutral pose moved by pelvisOffset and then tilted about the waist: `lean` forward (radians,
// the top toward +Z) and `roll` sideways (radians, the top toward +X, the juggler's left). The
// feet stay planted, so the legs bend to follow the pelvis.
struct BodyMotion {
    Vec3 pelvisOffset;        // from the neutral waist; y < 0 when crouching
    float lean = 0.0f;
    float roll = 0.0f;
    Vec3 lookAt{0.0f, 1.2f, 0.5f};  // what the head is looking at (mostly the highest balls)
    float intensity = 0.0f;   // 0..1: how fervent the juggling looks (see patternIntensity)
};

const Vec3 kNeutralWaist(0.0f, 0.98f, 0.0f);  // must match makeNeutralPose()

// Rotates a direction by the upper body's lean and roll.
inline Vec3 bodyDirection(const BodyMotion& m, Vec3 d) {
    const float cl = std::cos(m.lean), sl = std::sin(m.lean);
    const Vec3 leaned(d.x, d.y * cl - d.z * sl, d.y * sl + d.z * cl);
    const float cr = std::cos(m.roll), sr = std::sin(m.roll);
    return Vec3(leaned.x * cr + leaned.y * sr, -leaned.x * sr + leaned.y * cr, leaned.z);
}
// Where a point of the neutral upper body (shoulder, neck, ...) is with the body moved.
inline Vec3 bodyPoint(const BodyMotion& m, Vec3 neutral) {
    return kNeutralWaist + m.pelvisOffset + bodyDirection(m, neutral - kNeutralWaist);
}

// One juggler at a moment. Palms and body are in the juggler's own coordinates (their
// juggler-local frame); `position` and `yaw` place that frame in the world.
struct JugglerState {
    Vec3 position;
    float yaw = 0.0f;
    Vec3 palmRight, palmLeft;  // where each hand's palm is (a ball sits just above it; a club
                               // handle or ring rim passes through it)
    BodyMotion body;           // lookAt is in the juggler's own coordinates too
};

// Everything at a moment. Props and trails are in world coordinates.
struct JugglerScene {
    PropType prop = PropType::Ball;
    std::vector<JugglerState> jugglers;
    std::vector<BallState> balls;
    std::vector<Trail> trails;
};

// How much space the pattern uses (juggler-local), for framing the camera: the lowest point
// the hands (or what they hold) reach, the highest point any prop reaches, and how far out to
// the side things go.
struct SceneExtents {
    float lowestHandY = 0.0f;    // bottom of the hands at the lowest point of their scoop
    float highestPropY = 0.0f;   // top of the highest ball at the top of its flight
    float halfWidth = 0.0f;      // half the width (along X) of hands and balls, about centerX
    float centerX = 0.0f;        // middle of that width
    float centerZ = 0.0f;        // depth of the hands' throw points (where the camera aims)
};
// World extents of the whole pattern, or (with onlyJuggler >= 0) of one juggler: their hands,
// what they hold and their selfs (passes leave them, so they're left out).
SceneExtents computeSceneExtents(const JugglingLoop& loop, const JuggleParams& params,
                                 int onlyJuggler = -1);

// How fervent the pattern looks, 0..1: grows with how high the throws are (relative to the
// highest a person can really drive a throw, about 3.7 m) and how much the heights vary.
float patternIntensity(const JugglingLoop& loop, const JuggleParams& params);

// Evaluates the scene at `beat` (fractional; beat 0 is the right hand's first throw) for the
// repeating loop `loop` (every juggler's throws). orbits must be computeBallOrbits(loop).
JugglerScene evaluateScene(const JugglingLoop& loop, const BallOrbits& orbits,
                           const JuggleParams& params, double beat);
