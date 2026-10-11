// juggle_sim.cpp - see juggle_sim.h.
#include "juggle_sim.h"

#include "choreography.h"
#include "loop_ops.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace {

constexpr float kGravity = 9.81f;  // m/s^2, along -Y

int modPositive(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}


// Hand geometry (juggler-local). Throws are made from the inside, catches on the outside.
// Roughly elbow height, in front of the body, so the elbows stay comfortably bent. Higher
// throws (heightFraction toward 1) are made and caught wider.
Vec3 throwPoint(bool right, float heightFraction) {
    const float x = 0.09f + 0.07f * heightFraction;
    return Vec3(right ? -x : x, 1.12f, 0.36f);
}
Vec3 catchPoint(bool right, float heightFraction) {
    const float x = 0.26f + 0.08f * heightFraction;
    return Vec3(right ? -x : x, 1.18f, 0.36f);
}
const Vec3 kBallOnPalm(0.0f, 0.0125f + kBallRadius, 0.0f);  // ball center relative to palm
// A self 1 is a hand-across: the throwing hand carries the prop just past the middle and lets
// it go, and the other hand takes it right beside it (the hands nearly touch).
Vec3 handAcrossThrowPoint(bool right) { return Vec3(right ? 0.01f : -0.01f, 1.13f, 0.36f); }
Vec3 handAcrossCatchPoint(bool right) { return Vec3(right ? -0.10f : 0.10f, 1.14f, 0.36f); }

// Neutral shoulders and how far a palm can get from its shoulder (upper arm + forearm + a
// little of the hand, minus a margin). Must match the juggler figure's proportions
// (juggler_figure.cpp). The shoulders move with the body (bodyPoint).
Vec3 neutralShoulder(bool right) { return Vec3(right ? -0.19f : 0.19f, 1.43f, 0.0f); }
constexpr float kPalmReach = 0.58f;

// Pulls a point that's out of the arm's reach back onto the edge of it, toward the shoulder.
// A deep scoop therefore curves down and in toward the body, as real ones do.
Vec3 withinReach(Vec3 shoulderPos, Vec3 p) {
    const Vec3 d = p - shoulderPos;
    const float dist = length(d);
    return dist > kPalmReach ? shoulderPos + d * (kPalmReach / dist) : p;
}

// Hand dynamics (see palm() for how they're used).
constexpr double kMaxTrailBeats = 1.8;  // longest a flight's trail gets (see evaluateScene)
constexpr float kHandDriveAccel = 60.0f;   // m/s^2: how hard a hand can accelerate a throw
constexpr float kMaxArmDrive = 0.28f;      // m: the part of a drive the arms alone can make
constexpr float kMaxDrive = 0.60f;         // m: the longest drive, arms plus knees and back
constexpr float kMinScoop = 0.02f;         // m: even a gentle throw dips a little
constexpr float kHandStopAccel = 120.0f;   // m/s^2: how hard a hand stops after release
constexpr float kMaxFollowThrough = 0.12f; // m
constexpr float kCatchSoftness = 0.4f;     // hand moves at this fraction of the caught ball's velocity
constexpr float kMaxCatchHandSpeed = 2.5f; // m/s: but never faster than this at the catch

// The human limit: the highest throw (above the hands) a full-length drive at full acceleration
// can make, about 3.7 m. Higher throws still fly as physics says, but the body language
// stops growing there.
constexpr float kHumanLimitHeight = kMaxDrive * kHandDriveAccel / kGravity;

// Body motion (see bodyAt()).
constexpr float kBaseCrouch = 0.015f;      // m: knees slightly soft even for easy patterns
constexpr float kFervorCrouch = 0.035f;    // m: extra at full intensity
constexpr float kFervorBob = 0.04f;        // m: extra dip per throw at full intensity and height
constexpr float kBodySmoothing = 0.10f;     // s: body motion is smoothed over about this long
constexpr float kGazeSmoothing = 0.15f;
// Passing to (or catching from) someone who isn't straight ahead, as around a ring of 3+
// jugglers: the hands' throw and catch points turn most of the way toward that juggler (the
// club's spin plane with them), and the upper body twists part of the way, around each such
// throw. Partners straight ahead (two jugglers face to face) turn nothing.
constexpr float kPassTurnShare = 0.7f;   // of the angle to the partner, for the hands
constexpr float kMaxPassTurn = 0.9f;     // rad (about 50 degrees)
constexpr float kTwistShare = 0.5f;      // of the hands' turn, for the upper body
constexpr float kTwistSmoothing = 0.6f;  // beats: the twist eases in and out over about this     // s: and the head's gaze over this long
constexpr float kMaxBodySpeed = 1.2f;      // m/s: average speed of the hips sinking or rising
constexpr float kHipsBack = 0.35f;         // hips move back this much per meter of crouch
constexpr float kLeanPerCrouch = 0.6f;     // radians of forward lean per meter of crouch
constexpr float kFervorLean = 0.05f;       // radians
constexpr float kMaxSway = 0.08f;          // m: torso sway toward the driving hand, at the top
constexpr float kCrouchPerBodyDrive = 0.8f; // the lean lowers the shoulders too, so a bit less crouch
constexpr float kSwayHipShare = 0.35f;     // how much of the sway is the hips moving
constexpr float kTorsoHeight = 0.48f;      // waist to neck

// Cubic Hermite: position at time tau (seconds) along a segment of duration T from p0 (velocity
// v0) to p1 (velocity v1). Position and velocity are continuous with neighboring segments.
Vec3 hermite(Vec3 p0, Vec3 v0, Vec3 p1, Vec3 v1, float T, float tau) {
    const float u = T > 1e-6f ? std::clamp(tau / T, 0.0f, 1.0f) : 1.0f;
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u;
    const float h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    return p0 * h00 + v0 * (h10 * T) + p1 * h01 + v1 * (h11 * T);
}

// ---- Props. A ball sits on the palm. A club is held at its grip (between the collar and the
// knob), so its center of mass is out along the club from the hand; a ring is held at its
// rim, so its center is a ring-radius away. Clubs and rings spin as they fly.
constexpr float kClubGripToCenter = kClubGrip - kClubCenterOfMass;                // 0.172 m
constexpr float kRingGripToCenter = 0.5f * (kRingOuterRadius + kRingInnerRadius);  // band middle
// How far a prop reaches from its center of mass in any direction (for framing the camera).
float propReach(PropType prop) {
    switch (prop) {
        case PropType::Club: return std::max(kClubCenterOfMass, kClubLength - kClubCenterOfMass);
        case PropType::Ring: return kRingOuterRadius;
        default: return kBallRadius;
    }
}
// Only used to pick a whole number of spins for each throw (single, double, ...); the actual
// spin rate is that number of turns divided by the throw's own flight time, so the prop
// always arrives at the catching hand the right way round.
constexpr float kNaturalSpinRate = 1.7f;  // turns per second
// Clubs and rings spin about the line through the shoulders, the top turning back toward the
// juggler as they leave the hand (clockwise, seen from the juggler's left). The axis is turned
// about 7 degrees (counterclockwise seen from above for the right hand, clockwise for the
// left), so a prop leaves the hand pointing a little toward the other side.
constexpr float kClubAxisBias = 7.0f * kPi / 180.0f;

// The plane a prop spins in: at angle theta (radians) its axis points
// up * cos(theta) + dir * sin(theta). Theta 0 is upright; positive tips it toward `dir`, which
// points back toward the juggler, and props spin toward positive theta.
struct SpinFrame {
    Vec3 up{0.0f, 1.0f, 0.0f};
    Vec3 dir{0.0f, 0.0f, 1.0f};
    Vec3 at(float theta) const { return up * std::cos(theta) + dir * std::sin(theta); }
    Vec3 rate(float theta) const { return dir * std::cos(theta) - up * std::sin(theta); }  // d/dtheta
    Vec3 spinAxis() const { return cross(up, dir); }
};

// Where in its turn a prop is at the moments that matter (theta, radians):
//   release: as it leaves the hand;
//   caught: as it arrives in the next hand (after its whole number of spins);
//   cocked: the bottom of the scoop, ready to be flicked into the next throw.
// A club is caught with its top pointing forward and up (about 33 degrees above horizontal),
// hangs down from the wrist at the bottom of the scoop (top about 62 degrees below horizontal,
// forward), and is flicked up so it leaves the hand pointing forward, about 15 degrees above
// horizontal, already spinning (from slow-motion video of real club throws). A ring does the
// same, more gently.
struct PropAngles {
    float release = 0.0f, caught = 0.0f, cocked = 0.0f;
};
PropAngles propAngles(PropType prop) {
    switch (prop) {
        case PropType::Club: return {-1.31f, -1.00f, -2.65f};
        case PropType::Ring: return {0.10f, -0.35f, -0.50f};
        default: return {};
    }
}

// A carry shorter than this (seconds) winds the prop up less.
constexpr float kFullWindUpSeconds = 0.3f;
// Average turning speed of the flick from the cocked angle to the release (radians/second).
constexpr float kMaxFlickRate = 20.0f;
// The flick gets at least this share of the carry.
constexpr float kFlickShare = 0.4f;

// Scalar cubic Hermite (see hermite()).
float hermite1(float p0, float v0, float p1, float v1, float T, float tau) {
    const float u = T > 1e-6f ? std::clamp(tau / T, 0.0f, 1.0f) : 1.0f;
    const float u2 = u * u, u3 = u2 * u;
    return p0 * (2 * u3 - 3 * u2 + 1) + v0 * T * (u3 - 2 * u2 + u) + p1 * (-2 * u3 + 3 * u2) +
           v1 * T * (u3 - u2);
}

// One throw's drive: how far the hand travels accelerating it, and for how long.
struct Drive {
    float distance = kMinScoop;  // m
    float seconds = 0.0f;        // the scoop takes the rest of the carry
    float carrySeconds = 0.0f;   // from the catch to the release
};

// Where a pass is caught (juggler-local). Balls are caught where selfs are; clubs and rings
// about 20 cm outside the shoulder and 20 cm in front of the body, higher for higher passes.
Vec3 passCatchPoint(bool right, float heightFraction) {
    const float x = 0.39f;
    return Vec3(right ? -x : x, 1.22f + 0.18f * heightFraction, 0.25f);
}

// A juggler's local frame in the world: a position, and a turn about +Y (yaw) that takes their
// facing direction (+Z) to (sin yaw, 0, cos yaw). Matches Mat4::translation * Mat4::rotationY.
struct Placement {
    Vec3 position;
    float yaw = 0.0f;
    float cosYaw = 1.0f, sinYaw = 0.0f;
    Vec3 dir(Vec3 d) const { return Vec3(d.x * cosYaw + d.z * sinYaw, d.y, -d.x * sinYaw + d.z * cosYaw); }
    Vec3 point(Vec3 p) const { return position + dir(p); }
    Vec3 localDir(Vec3 d) const { return Vec3(d.x * cosYaw - d.z * sinYaw, d.y, d.x * sinYaw + d.z * cosYaw); }
    Vec3 localPoint(Vec3 p) const { return localDir(p - position); }
};

// Everything below works per juggler (j) and beat (b). Every juggler throws on every beat,
// right hand on even beats. A throw of value v from (j, b) is thrown again by juggler
// dest(j, b) on beat b + v: the same juggler for a self, another for a pass.
// The loop the physics runs on: a sketch's open throws become empty hands (0s), which the
// hands simply circle through. (What lands in them is handled by evaluateScene.)
JugglingLoop physicsLoop(const JugglingLoop& loop) {
    JugglingLoop phys = loop;
    for (size_t i = 0; i < phys.throws.size(); ++i)
        if (phys.throws[i].value == kOpenThrow) phys.throws[i] = {0, static_cast<int>(i) / std::max(1, loop.period)};
    return phys;
}

class Evaluator {
public:
    Evaluator(const JugglingLoop& loop, const JuggleParams& params, float intensity = 0.0f)
        : loop_(loop), period_(loop.period), jugglers_(loop.jugglers), params_(params), intensity_(intensity) {
        // With a choreography the jugglers move: where they are depends on the time. Otherwise
        // they stand still, in the default formation.
        cycle_ = choreographyCycle(loop, params);
        moving_ = cycle_ > 0;
        for (int k = 0; k < jugglers_; ++k) {
            Vec3 position;
            float yaw = 0.0f;
            jugglerPlacement(loop, params, k, &position, &yaw);
            placements_.push_back(makePlacement(position, yaw));
        }
        turnTo_.assign(static_cast<size_t>(jugglers_ * jugglers_), 0.0f);
        for (int k = 0; k < jugglers_; ++k)
            for (int o = 0; o < jugglers_; ++o) {
                if (o == k) continue;
                const Vec3 local = placements_[static_cast<size_t>(k)].localPoint(placements_[static_cast<size_t>(o)].position);
                const float angle = std::atan2(local.x, local.z);
                turnTo_[static_cast<size_t>(k * jugglers_ + o)] =
                    std::fabs(angle) < 1e-4f ? 0.0f : std::clamp(kPassTurnShare * angle, -kMaxPassTurn, kMaxPassTurn);
            }
        // The throw landing on each slot: its value and who threw it (0 / -1 if none).
        const size_t slots = static_cast<size_t>(jugglers_ * period_);
        incomingValue_.assign(slots, 0);
        incomingFrom_.assign(slots, -1);
        for (int k = 0; k < jugglers_; ++k) {
            for (int b = 0; b < period_; ++b) {
                const LoopThrow& t = loop_.at(k, b);
                if (t.value <= 0) continue;
                const size_t landing = static_cast<size_t>(t.dest * period_ + modPositive(b + t.value, period_));
                incomingValue_[landing] = t.value;
                incomingFrom_[landing] = k;
            }
        }
        // Everything repeats after `span_` beats: the hands (two loops if the loop is odd, so
        // each beat is seen from both hands) and, with a choreography, the walking too.
        span_ = period_ % 2 == 0 ? period_ : 2 * period_;
        if (moving_) span_ = lcm(span_, cycle_);
    }

    int jugglers() const { return jugglers_; }
    int span() const { return span_; }  // beats after which everything repeats
    // Where j is (and faces) at beat t.
    Placement placement(int j, double t) const {
        if (!moving_) return placements_[static_cast<size_t>(j)];
        Vec3 position;
        float yaw = 0.0f;
        jugglerPlacementAt(loop_, params_, j, t, &position, &yaw);
        return makePlacement(position, yaw);
    }
    // How fast j is walking at beat t (world, m/s).
    Vec3 walkVelocity(int j, double t) const {
        if (!moving_) return Vec3(0.0f, 0.0f, 0.0f);
        const double h = 0.02;
        return (placement(j, t + h).position - placement(j, t - h).position) *
               static_cast<float>(1.0 / (2.0 * h * secondsPerBeat()));
    }
    static Placement makePlacement(Vec3 position, float yaw) {
        Placement pl;
        pl.position = position;
        pl.yaw = yaw;
        pl.cosYaw = std::cos(yaw);
        pl.sinYaw = std::sin(yaw);
        return pl;
    }
    static int lcm(int a, int b) {
        int x = a, y = b;
        while (y != 0) {
            const int r = x % y;
            x = y;
            y = r;
        }
        return a / x * b;
    }

    int valueAt(int j, int b) const { return loop_.at(j, b).value; }
    int destAt(int j, int b) const { return loop_.at(j, b).dest; }
    // A 2 thrown to yourself is a hold: the prop stays in the hand. (A 2p is a real pass.)
    bool isHeld(int j, int b) const { return valueAt(j, b) == 2 && destAt(j, b) == j; }
    int incomingAt(int j, int b) const {
        return incomingValue_[static_cast<size_t>(j * period_ + modPositive(b, period_))];
    }
    int incomingFrom(int j, int b) const {
        return incomingFrom_[static_cast<size_t>(j * period_ + modPositive(b, period_))];
    }
    bool incomingHeld(int j, int b) const { return incomingAt(j, b) == 2 && incomingFrom(j, b) == j; }
    bool incomingPass(int j, int b) const { return incomingAt(j, b) > 0 && incomingFrom(j, b) != j; }

    // Dwell (beats) of the catch made before juggler j throws on beat b.
    double catchDwell(int j, int b) const {
        const int vIn = incomingAt(j, b);
        if (vIn == 0) return params_.dwellBeats;                          // empty: the hand still scoops
        if (incomingHeld(j, b)) return std::min(params_.dwellBeats, 1.0);  // a held 2: same rhythm
        return std::min(params_.dwellBeats, 0.5 * vIn);
    }

    double secondsPerBeat() const { return 60.0 / params_.bpm; }
    double catchTimeFor(int j, int b) const { return b - catchDwell(j, b); }

    // How high the throw made by j on beat b goes above the hands, as a fraction of the human
    // limit (0 for empty beats and held 2s, which aren't thrown). Depends only on timing.
    float heightFraction(int j, int b) const {
        const int v = valueAt(j, b);
        if (v <= 0 || isHeld(j, b)) return 0.0f;
        const float T = flightSeconds(j, b, v);
        const float height = kGravity * T * T / 8.0f;
        return std::clamp(height / kHumanLimitHeight, 0.0f, 1.0f);
    }
    // Same, for the throw j catches before throwing on beat b.
    float incomingHeightFraction(int j, int b) const {
        const int vIn = incomingAt(j, b);
        return vIn > 0 ? heightFraction(incomingFrom(j, b), b - vIn) : 0.0f;
    }

    // Where j's hand releases on beat b and catches before throwing on beat b (j-local).
    Vec3 throwPointAt(int j, int b) const {
        if (valueAt(j, b) == 1 && destAt(j, b) == j) return handAcrossThrowPoint(rightHand(j, b));
        return turned(throwPoint(rightHand(j, b), heightFraction(j, b)), throwTurn(j, b));
    }
    Vec3 catchPointAt(int j, int b) const {
        if (incomingAt(j, b) == 1 && incomingFrom(j, b) == j) return handAcrossCatchPoint(rightHand(j, b));
        if (incomingPass(j, b) && prop() != PropType::Ball)
            return turned(passCatchPoint(rightHand(j, b), incomingHeightFraction(j, b)), catchTurn(j, b));
        return turned(catchPoint(rightHand(j, b), incomingHeightFraction(j, b)), catchTurn(j, b));
    }

    // ---- Turning toward a partner who isn't straight ahead (radians about +Y, positive toward
    // the juggler's left, +X): for the pass j throws on beat b, and for one arriving on beat b.
    float throwTurn(int j, int b) const {
        const int v = valueAt(j, b);
        if (v <= 0 || isHeld(j, b)) return 0.0f;
        return turnTo(j, destAt(j, b), b);
    }
    float catchTurn(int j, int b) const {
        if (!incomingPass(j, b)) return 0.0f;
        return turnTo(j, incomingFrom(j, b), b);
    }
    // Whether j throws with the right hand on beat b (their hands may be swapped: LRswap).
    bool rightHand(int j, int b) const { return loop_.rightHandBeat(j, b); }
    // How far j's hands turn toward `other` around beat t.
    float turnTo(int j, int other, double t) const {
        if (!moving_) return turnTo_[static_cast<size_t>(j * jugglers_ + other)];
        if (other == j) return 0.0f;
        const Vec3 local = placement(j, t).localPoint(placement(other, t).position);
        const float angle = std::atan2(local.x, local.z);
        return std::fabs(angle) < 1e-4f ? 0.0f : std::clamp(kPassTurnShare * angle, -kMaxPassTurn, kMaxPassTurn);
    }
    // A juggler-local point or direction turned by `angle` about the vertical through the feet.
    static Vec3 turned(Vec3 p, float angle) {
        if (angle == 0.0f) return p;
        const float c = std::cos(angle), s = std::sin(angle);
        return Vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
    }
    // How far j's upper body is twisted at beat t: part of the way toward the partners of the
    // passes thrown and caught around then, eased in and out.
    float twistAt(int j, double t) const {
        if (jugglers_ < 3) return 0.0f;
        float sum = 0.0f, weightSum = 0.0f;
        const int first = static_cast<int>(std::floor(t)) - 2;
        for (int n = first; n <= first + 5; ++n) {
            const float u = static_cast<float>(t - n) / kTwistSmoothing;
            const float w = std::exp(-0.5f * u * u);
            const float thrown = throwTurn(j, n);
            sum += w * (thrown != 0.0f ? thrown : catchTurn(j, n));
            weightSum += w;
        }
        return kTwistShare * sum / weightSum;
    }

    // ---- Props: how each throw's prop is turned and how fast it spins.
    PropType prop() const { return params_.prop; }
    float gripToCenter() const {
        return prop() == PropType::Club ? kClubGripToCenter
                                        : (prop() == PropType::Ring ? kRingGripToCenter : 0.0f);
    }
    // The plane the throw made on beat b spins in (thrower-local): clubs and rings both spin end
    // over end, in the plane running front to back through the juggler (turned by
    // kClubAxisBias), the top turning back toward the juggler.
    SpinFrame frameFor(int j, int b) const {
        const bool right = rightHand(j, b);
        SpinFrame f;
        if (prop() != PropType::Ball) {
            const float side = std::sin(kClubAxisBias);
            f.dir = turned(Vec3(right ? -side : side, 0.0f, -std::cos(kClubAxisBias)), throwTurn(j, b));
        }
        return f;
    }
    // Center of the prop relative to the hand (palm) holding it.
    Vec3 propOffset(const SpinFrame& f, float theta) const {
        if (prop() == PropType::Ball) return kBallOnPalm;
        return f.at(theta) * gripToCenter();
    }
    // A pass between jugglers facing each other arrives spinning the other way round in the
    // catcher's own frame (the top turns back toward the thrower, which is away from the
    // catcher). flips() says whether that's so for the throw j makes on beat b.
    bool flips(int j, int b) const {
        if (prop() == PropType::Ball) return false;
        const int r = destAt(j, b);
        if (r == j) return false;
        const Vec3 thrower = placement(j, b).dir(frameFor(j, b).dir);
        const Vec3 catcher = placement(r, b + valueAt(j, b)).dir(frameFor(r, b + valueAt(j, b)).dir);
        return dot(thrower, catcher) < 0.0f;
    }
    // The angle (in the thrower's frame) the throw arrives at: whatever puts it at the caught
    // angle in the catcher's frame.
    float arrivalAngle(int j, int b) const {
        const float caught = propAngles(prop()).caught;
        return flips(j, b) ? -caught : caught;
    }
    // The prop's spin frame as the catcher sees it (catcher-local), and the sign that turns
    // the thrower's angles and spin into the catcher's.
    SpinFrame arrivalFrame(int j, int b, float* sign) const {
        const int r = destAt(j, b);
        const float s = flips(j, b) ? -1.0f : 1.0f;
        SpinFrame f;
        f.dir = placement(r, b + valueAt(j, b)).localDir(placement(j, b).dir(frameFor(j, b).dir)) * s;
        if (sign) *sign = s;
        return f;
    }
    // Whole turns the throw j makes on beat b (value v) makes in the air: about
    // kNaturalSpinRate turns a second of flight, at least one. 1s go across flat (no turn);
    // held 2s don't fly.
    int spins(int j, int b, int v) const {
        if (prop() == PropType::Ball || v <= 1 || isHeld(j, b)) return 0;
        if (v == 2 && destAt(j, b) == j) return 0;
        const float turns = flightSeconds(j, b, v) * kNaturalSpinRate;
        return std::max(1, static_cast<int>(std::lround(turns)));
    }
    // Spin rate in the air (radians/second, thrower's frame): the spins plus getting from the
    // release angle to the arrival angle, over exactly the flight time.
    float spinRate(int j, int b, int v) const {
        if (prop() == PropType::Ball || v <= 0 || isHeld(j, b)) return 0.0f;
        const float turn = 2.0f * kPi * static_cast<float>(spins(j, b, v)) + arrivalAngle(j, b) -
                           propAngles(prop()).release;
        return turn / flightSeconds(j, b, v);
    }

    // ---- Flights (world). The prop's center of mass flies on the parabola. Its endpoints are
    // where the center is when the hand is at the throw point (release) and at the catch point
    // (catch), so flights don't depend on the hand paths.
    Vec3 launchPoint(int j, int b) const {
        return placement(j, b).point(throwPointAt(j, b) + propOffset(frameFor(j, b), propAngles(prop()).release));
    }
    Vec3 landingPoint(int j, int b, int v) const {
        const int r = destAt(j, b);
        const SpinFrame f = arrivalFrame(j, b, nullptr);
        // Where the catcher is when they catch it (they may be walking).
        return placement(r, catchTimeFor(r, b + v)).point(catchPointAt(r, b + v) + propOffset(f, propAngles(prop()).caught));
    }
    float flightSeconds(int j, int b, int v) const {
        return static_cast<float>((catchTimeFor(destAt(j, b), b + v) - b) * secondsPerBeat());
    }
    // Launch velocity: chosen so the parabola from the launch point reaches the landing point
    // exactly when the catch is due.
    Vec3 launchVelocity(int j, int b, int v) const {
        const float T = flightSeconds(j, b, v);
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        return (landingPoint(j, b, v) - launchPoint(j, b)) * (1.0f / T) - accel * (0.5f * T);
    }
    // Position of a prop's center in flight: thrown by j on beat b (value v), at time t.
    Vec3 flightPosition(int j, int b, int v, double t) const {
        const float tau = static_cast<float>((t - b) * secondsPerBeat());
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        return launchPoint(j, b) + launchVelocity(j, b, v) * tau + accel * (0.5f * tau * tau);
    }
    // The whole prop in flight: center, and the turn it has made since the release.
    void flightProp(int j, int b, int v, double t, BallState* out) const {
        out->prop = prop();
        out->center = flightPosition(j, b, v, t);
        const SpinFrame f = frameFor(j, b);
        const float tau = static_cast<float>((t - b) * secondsPerBeat());
        const float theta = propAngles(prop()).release + spinRate(j, b, v) * tau;
        const Placement pl = placement(j, b);
        out->axis = pl.dir(f.at(theta));
        out->spinAxis = pl.dir(f.spinAxis());
    }

    // Velocity j's hand has when it releases on beat b (j-local): whatever makes the prop's
    // center leave at its launch velocity. For a ball that's the launch velocity itself; a club
    // or ring is also turning about the hand, which carries its center along, so the hand moves
    // that much less. Rest for a held 2 or an empty beat.
    Vec3 releaseVelocity(int j, int b) const {
        const int v = valueAt(j, b);
        if (v <= 0 || isHeld(j, b)) return Vec3(0.0f, 0.0f, 0.0f);
        const float turning = spinRate(j, b, v) * gripToCenter();
        // (Relative to the juggler, who may be walking.)
        const Placement pl = placement(j, b);
        const Vec3 world = launchVelocity(j, b, v) - pl.dir(frameFor(j, b).rate(propAngles(prop()).release)) * turning -
                           walkVelocity(j, b);
        return pl.localDir(world);
    }
    // Velocity j's hand has at the catch before throwing on beat b (j-local): a fraction of the
    // incoming prop's velocity, so the hand "gives" with the catch (an inelastic collision).
    Vec3 catchVelocity(int j, int b) const {
        const int vIn = incomingAt(j, b);
        if (vIn <= 0 || incomingHeld(j, b)) return Vec3(0.0f, 0.0f, 0.0f);
        const int src = incomingFrom(j, b);
        const int from = b - vIn;
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        const Vec3 centerVelocity = launchVelocity(src, from, vIn) + accel * flightSeconds(src, from, vIn);
        const float turning = spinRate(src, from, vIn) * gripToCenter();
        const double catchTime = catchTimeFor(j, b);
        const Vec3 gripVelocity = centerVelocity -
                                  placement(src, from).dir(frameFor(src, from).rate(arrivalAngle(src, from))) * turning -
                                  walkVelocity(j, catchTime);
        return placement(j, catchTime).localDir(gripVelocity) * kCatchSoftness;
    }

    // The drive for j's release on beat b: v^2 / 2a at the hand's top acceleration, up to the
    // longest drive arms and body can make together. Past that (throws above the human limit)
    // the hand still reaches the release speed, just with more than human acceleration.
    // The speed is the prop's launch speed: for a club or ring the wrist's flick adds some of
    // that at the release, but the arm still drives the prop's whole mass up to speed.
    Drive driveFor(int j, int b) const {
        Drive d;
        const int v = valueAt(j, b);
        const float speed = (v > 0 && !isHeld(j, b)) ? length(launchVelocity(j, b, v)) : 0.0f;
        d.carrySeconds = static_cast<float>(catchDwell(j, b) * secondsPerBeat());
        d.distance = std::clamp(speed * speed / (2.0f * kHandDriveAccel), kMinScoop, kMaxDrive);
        d.seconds = speed > 0.05f ? 2.0f * d.distance / speed : 0.5f * d.carrySeconds;
        d.seconds = std::min(d.seconds, 0.7f * d.carrySeconds);
        return d;
    }

    // ---- Body (per juggler). Each throw on beat n contributes a smooth bump in time: it
    // starts at the catch, peaks where the hand's drive begins, and dies away just after the
    // release (the body is still rising as the ball leaves). The crouch is the deepest of the
    // bumps going on (so a run of high throws keeps the juggler low, bobbing on each), and the
    // sway adds them up, alternating sides with the hands.
    //
    // Returns the bump's shape (0..1) at time t (seconds) for j's throw on beat n.
    float bumpAt(int j, int n, double tSeconds) const {
        const ThrowInfo& info = throwInfo(j, n);
        const double spb = secondsPerBeat();
        const double start = (n - info.catchDwell) * spb;
        const double release = n * spb;
        const double peak = release - info.drive.seconds;
        const double end = release + 0.6 * info.drive.seconds + 0.05 * spb;
        if (tSeconds <= start || tSeconds >= end) return 0.0f;
        if (tSeconds < peak) {
            const double u = (tSeconds - start) / std::max(1e-6, peak - start);
            return static_cast<float>(0.5 - 0.5 * std::cos(kPi * u));
        }
        const double u = (tSeconds - peak) / std::max(1e-6, end - peak);
        return static_cast<float>(0.5 + 0.5 * std::cos(kPi * u));
    }

    // The body's targets at one instant: how far the hips dip below the stance, and the sway.
    // Where two throws' dips overlap, they blend smoothly (a soft maximum: about the deeper
    // one) rather than switching abruptly from one to the other.
    void rawBody(int j, double tSeconds, float* dip, float* sway) const {
        const double spb = secondsPerBeat();
        const int now = static_cast<int>(std::floor(tSeconds / spb));
        float dip4 = 0.0f;
        *sway = 0.0f;
        // A throw's bump lasts from its catch (under 2 beats before it) to just after it.
        for (int n = now - 1; n <= now + 2; ++n) {
            const float shape = bumpAt(j, n, tSeconds);
            if (shape <= 0.0f) continue;
            const ThrowInfo& info = throwInfo(j, n);
            const float d = shape * info.dip;
            dip4 += (d * d) * (d * d);
            // The two hands' bumps overlap (one hand scoops while the other is still holding),
            // so the sway uses a sharpened bump that's only large around this hand's drive;
            // otherwise the sides would cancel out.
            const float side = rightHand(j, n) ? -1.0f : 1.0f;
            *sway += side * shape * shape * shape * info.sway;
        }
        *dip = std::sqrt(std::sqrt(dip4));
    }

    // j's body at beat t. The targets above say where each throw wants the body; a real body
    // has mass and can't follow them instantly (the hips can't even drop faster than gravity),
    // so they're smoothed over time (a Gaussian average over about kBodySmoothing either side).
    // Quick changes, like a deep dip for every beat of a fast high pattern, come out as a
    // steadier crouch with a small bob, much as they do for a real juggler. Smoothing looks
    // ahead as well as back, so the body anticipates a big throw a little, as people do.
    BodyMotion bodyAt(int j, double t) const {
        BodyMotion m;
        m.intensity = intensity_;
        const double tSeconds = t * secondsPerBeat();
        const int kTaps = bodyTaps_;  // each side
        float dip = 0.0f, sway = 0.0f, weightSum = 0.0f;
        for (int k = -kTaps; k <= kTaps; ++k) {
            const float u = 3.0f * static_cast<float>(k) / kTaps;  // in sigmas
            const float w = std::exp(-0.5f * u * u);
            float d = 0.0f, sw = 0.0f;
            rawBody(j, tSeconds + u * kBodySmoothing, &d, &sw);
            dip += w * d;
            sway += w * sw;
            weightSum += w;
        }
        dip /= weightSum;
        sway /= weightSum;
        const float crouch = kBaseCrouch + kFervorCrouch * intensity_ + dip;
        m.pelvisOffset = Vec3(kSwayHipShare * sway, -crouch, -kHipsBack * crouch);
        m.lean = kLeanPerCrouch * crouch + kFervorLean * intensity_;
        m.roll = std::atan2((1.0f - kSwayHipShare) * sway, kTorsoHeight);
        m.twist = twistAt(j, t);
        return m;  // lookAt is filled in separately (gazeAt): the hands don't need it
    }

    // Where j's head looks (world): mostly at the highest balls. A weighted average of the
    // balls in flight (everyone's), weighting each by (height above the hands)^4, plus a resting
    // point: in front of the hands for a solo juggler, the partner's chest when passing (so the
    // gaze splits between the partner and the props). The weights go by how heights compare,
    // not by meters, so two high balls passing each other hand the gaze over just as gently as
    // two low ones. A head has mass too, so this is smoothed over time like the body.
    Vec3 rawGaze(int j, double tSeconds) const {
        auto gazeWeight = [](float heightAboveHands) {
            const float h = std::max(heightAboveHands, 0.0f) + 0.05f;
            return (h * h) * (h * h);
        };
        const double spb = secondsPerBeat();
        const double t = tSeconds / spb;
        Vec3 sum(0.0f, 0.0f, 0.0f);
        float weightSum = 0.0f;
        if (jugglers_ == 1) {
            const float w = gazeWeight(0.25f);
            sum = placement(j, t).point(Vec3(0.0f, 1.2f, 0.5f)) * w;
            weightSum = w;
        } else {
            for (int k = 0; k < jugglers_; ++k) {
                if (k == j) continue;
                const float w = gazeWeight(0.5f);
                sum = sum + placement(k, t).point(Vec3(0.0f, 1.35f, 0.0f)) * w;
                weightSum += w;
            }
        }
        const int now = static_cast<int>(std::floor(t));
        for (int k = 0; k < jugglers_; ++k) {
            for (int b = now - 36; b <= now; ++b) {  // flights last under 36 beats
                const ThrowInfo& info = throwInfo(k, b);
                if (info.flightBeats <= 0.0f || t < b || t >= b + info.flightBeats) continue;
                const float tau = static_cast<float>(tSeconds - b * spb);
                const Vec3 p = info.launch + info.launchVelocity * tau + Vec3(0.0f, -0.5f * kGravity * tau * tau, 0.0f);
                // Fade each ball's weight in after the throw and out before the catch, so balls
                // joining and leaving the set don't make the gaze jump.
                const float edge = std::sin(kPi * static_cast<float>((t - b) / info.flightBeats));
                const float w = gazeWeight(p.y - 1.1f) * edge * edge;
                sum = sum + p * w;
                weightSum += w;
            }
        }
        return sum * (1.0f / weightSum);
    }
    Vec3 gazeAt(int j, double tSeconds) const {
        const int kTaps = 12;  // each side
        Vec3 sum(0.0f, 0.0f, 0.0f);
        float weightSum = 0.0f;
        for (int k = -kTaps; k <= kTaps; ++k) {
            const float u = 3.0f * static_cast<float>(k) / kTaps;
            const float w = std::exp(-0.5f * u * u);
            sum = sum + rawGaze(j, tSeconds + u * kGazeSmoothing) * w;
            weightSum += w;
        }
        return sum * (1.0f / weightSum);
    }
    Vec3 shoulderAt(int j, bool right, double t) const {
        return bodyPoint(bodyAt(j, t), neutralShoulder(right));
    }

    // ---- Hands (j-local). Each hand's cycle, from one of its releases (prev) to the next
    // (next):
    //   1. follow-through: leaves the throw point at the release velocity and decelerates to a
    //      stop a short way along it;
    //   2. return: swings out to the catch point, arriving at the catch velocity;
    //   3. scoop: gives with the ball down to a low point, at rest;
    //   4. drive: accelerates up to the throw point, reaching the next release velocity.
    // The scoop's depth is the drive (see driveFor), so higher throws scoop deeper; the body
    // crouches for the part of it the arms can't do. Every segment is a Hermite curve, so
    // position and velocity are continuous all the way round.
    Vec3 palm(int j, bool right, double t) const {
        int prev = static_cast<int>(std::floor(t));
        if (rightHand(j, prev) != right) --prev;
        const int next = prev + 2;
        const double spb = secondsPerBeat();
        const double catchTime = catchTimeFor(j, next);
        const Vec3 tpPrev = throwPointAt(j, prev), tp = throwPointAt(j, next), cp = catchPointAt(j, next);
        const Vec3 zero(0.0f, 0.0f, 0.0f);

        // 1. Follow-through after the release on `prev`.
        const Vec3 vOut = releaseVelocity(j, prev);
        const float emptySeconds = static_cast<float>((catchTime - prev) * spb);
        const float outSpeed = length(vOut);
        float followDist = std::min(outSpeed * outSpeed / (2.0f * kHandStopAccel), kMaxFollowThrough);
        float followSeconds = outSpeed > 1e-3f ? 2.0f * followDist / outSpeed : 0.0f;
        if (followSeconds > 0.45f * emptySeconds) {  // not enough time: shorten it
            followSeconds = 0.45f * emptySeconds;
            followDist = 0.5f * outSpeed * followSeconds;
        }
        const Vec3 followPoint = withinReach(shoulderAt(j, right, prev + followSeconds / spb),
                                             tpPrev + normalize(vOut) * followDist);
        const float returnSeconds = emptySeconds - followSeconds;

        // 3-4. Scoop down to the low point, then drive up to the release on `next`.
        const Vec3 vIn = releaseVelocity(j, next);
        const Drive drive = driveFor(j, next);
        const float driveSeconds = drive.seconds;
        // The low point is back along the direction of the throw (straight down for a vertical
        // one), pulled in toward the body if that's out of reach (with the body where it is
        // at that moment).
        const Vec3 driveDir = length(vIn) > 0.05f ? normalize(vIn) : Vec3(0.0f, 1.0f, 0.0f);
        const double lowTime = next - driveSeconds / spb;
        const Vec3 lowPoint = withinReach(shoulderAt(j, right, lowTime), tp - driveDir * drive.distance);
        const float scoopSeconds = drive.carrySeconds - driveSeconds;

        // 2/3. The hand's velocity at the catch: it gives with the ball, but no faster than a
        // hand plausibly moves, and no faster than the segments on either side can absorb
        // without swinging wide (a Hermite segment overshoots if its end speed is much more than
        // its average speed).
        Vec3 vCatch = catchVelocity(j, next);
        float catchSpeed = length(vCatch);
        float speedCap = kMaxCatchHandSpeed;
        if (returnSeconds > 1e-4f) speedCap = std::min(speedCap, 1.5f * length(cp - followPoint) / returnSeconds);
        if (scoopSeconds > 1e-4f) speedCap = std::min(speedCap, 1.5f * length(lowPoint - cp) / scoopSeconds);
        if (catchSpeed > speedCap) vCatch = vCatch * (speedCap / catchSpeed);

        if (t < catchTime) {
            const float tau = static_cast<float>((t - prev) * spb);
            if (tau < followSeconds) return hermite(tpPrev, vOut, followPoint, zero, followSeconds, tau);
            return hermite(followPoint, zero, cp, vCatch, returnSeconds, tau - followSeconds);
        }
        const float tau = static_cast<float>((t - catchTime) * spb);
        if (tau < scoopSeconds) return hermite(cp, vCatch, lowPoint, zero, scoopSeconds, tau);
        return hermite(lowPoint, zero, tp, vIn, driveSeconds, tau - scoopSeconds);
    }

    // The prop j's hand holds at time t (if it holds one), in the world: from the catch, the
    // prop's turn carries on a little (the hand gives with it), swings to the cocked angle by
    // the bottom of the scoop, and is flicked through the drive so it leaves the hand at the
    // release angle with exactly the next throw's spin. Between releases, only a held 2 is still
    // in the hand, resting at the release angle. The plane it turns in moves over from the
    // throwing hand's to this hand's during the carry.
    void heldProp(int j, bool right, double t, BallState* out) const {
        out->prop = prop();
        const Vec3 p = palm(j, right, t);
        int prev = static_cast<int>(std::floor(t));
        if (rightHand(j, prev) != right) --prev;
        const int next = prev + 2;
        const PropAngles a = propAngles(prop());
        SpinFrame frame;
        float theta = a.release;
        const double catchTime = catchTimeFor(j, next);
        if (t < catchTime) {
            frame = frameFor(j, prev);
        } else {
            const int vIn = incomingAt(j, next);
            SpinFrame from = frameFor(j, next);
            float startAngle = a.release, startRate = 0.0f;
            if (incomingHeld(j, next)) {
                from = frameFor(j, next - 2);
            } else if (vIn > 0) {
                const int src = incomingFrom(j, next);
                float sign = 1.0f;
                from = arrivalFrame(src, next - vIn, &sign);
                startAngle = a.caught;
                startRate = kCatchSoftness * spinRate(src, next - vIn, vIn) * sign;
            }
            const SpinFrame to = frameFor(j, next);
            const Drive drive = driveFor(j, next);
            // The flick (cocked to release) takes the end of the carry: the hand's drive, or
            // more if the drive is short (the wrist starts turning the prop up before the hand
            // starts driving).
            const float flickSeconds =
                std::min(std::max(drive.seconds, kFlickShare * drive.carrySeconds), 0.8f * drive.carrySeconds);
            const float dropSeconds = drive.carrySeconds - flickSeconds;
            const float tau = static_cast<float>((t - catchTime) * secondsPerBeat());
            const int v = valueAt(j, next);
            const float endRate = (v > 0 && !isHeld(j, next)) ? spinRate(j, next, v) : 0.0f;
            // The wind-up is as big as there's time for: a quick carry only cocks a little.
            const float fullWindUp = std::fabs(a.cocked - a.release);
            const float windUp = std::min(std::clamp(drive.carrySeconds / kFullWindUpSeconds, 0.2f, 1.0f),
                                          kMaxFlickRate * flickSeconds / std::max(fullWindUp, 1e-3f));
            const float cocked = a.release + (a.cocked - a.release) * windUp;
            theta = tau < dropSeconds
                        ? hermite1(startAngle, startRate, cocked, 0.0f, dropSeconds, tau)
                        : hermite1(cocked, 0.0f, a.release, endRate, flickSeconds, tau - dropSeconds);
            const float u = std::clamp(tau / std::max(drive.carrySeconds, 1e-4f), 0.0f, 1.0f);
            const float w = u * u * (3.0f - 2.0f * u);  // smoothstep: no kink at either end
            frame.dir = normalize(from.dir * (1.0f - w) + to.dir * w);
        }
        const Placement pl = placement(j, t);
        out->center = pl.point(p + propOffset(frame, theta));
        out->axis = pl.dir(frame.at(theta));
        out->spinAxis = pl.dir(frame.spinAxis());
    }

    // Smooths the body with fewer samples (see bodyAt): a little rougher, and several times
    // quicker, which is plenty for framing the camera.
    void useQuickBody() { bodyTaps_ = 5; }

private:
    int bodyTaps_ = 20;  // samples each side for bodyAt's smoothing
    const JugglingLoop& loop_;
    int period_;
    int jugglers_;
    JuggleParams params_;
    std::vector<Placement> placements_;  // standing still: where each juggler is
    bool moving_ = false;                 // a choreography moves them
    int cycle_ = 0;                       // ...repeating every cycle_ beats
    std::vector<float> turnTo_;  // [j * jugglers + other]: how far j's hands turn toward other
    struct ThrowInfo {
        int value = 0;
        double catchDwell = 0.0;
        Drive drive;
        float dip = 0.0f;    // how far this throw wants the hips to dip
        float sway = 0.0f;   // how far it wants the torso to sway toward its hand
        Vec3 launch, launchVelocity;  // world
        float flightBeats = 0.0f;  // 0 if not thrown (empty beat or a held 2)
    };
    // Per-throw data used many times a frame by the body and gaze, worked out when first asked
    // for: only the throws around the moment being drawn are (the span can be long, when the
    // walking and the throws take a long time to line up).
    const ThrowInfo& throwInfo(int j, int beat) const {
        const int b = modPositive(beat, span_);
        const long long key = static_cast<long long>(j) * span_ + b;
        const std::unordered_map<long long, ThrowInfo>::const_iterator found = throwCache_.find(key);
        if (found != throwCache_.end()) return found->second;
        ThrowInfo info;
        const int k = j;
        info.value = valueAt(k, b);
        info.catchDwell = catchDwell(k, b);
        info.drive = driveFor(k, b);
        const float hf = heightFraction(k, b);
        const float bodyDrive = std::max(0.0f, info.drive.distance - kMaxArmDrive);
        // A body can only sink and rise so fast: with too little time (a short dwell at a slow
        // tempo), it dips less.
        const float bumpSeconds = std::min(info.drive.carrySeconds - info.drive.seconds, 1.6f * info.drive.seconds);
        const float maxDip = kMaxBodySpeed * std::max(bumpSeconds, 0.0f);
        info.dip = std::min(kCrouchPerBodyDrive * bodyDrive + kFervorBob * intensity_ * hf, maxDip);
        info.sway = std::min(kMaxSway * hf * (0.4f + 0.6f * intensity_), 0.4f * maxDip);
        if (info.value > 0 && !isHeld(k, b)) {
            info.launch = launchPoint(k, b);
            info.launchVelocity = launchVelocity(k, b, info.value);
            info.flightBeats = static_cast<float>(catchTimeFor(destAt(k, b), b + info.value) - b);
        }
        return throwCache_.emplace(key, info).first->second;
    }

    float intensity_;
    std::vector<int> incomingValue_, incomingFrom_;
    int span_ = 0;
    mutable std::unordered_map<long long, ThrowInfo> throwCache_;
};

}  // namespace

double defaultPassingDistance(const JugglingLoop& loop) {
    int highest = 1;
    for (const LoopThrow& t : loop.throws) highest = std::max(highest, t.value);
    return std::clamp(1.0 + 0.4 * (highest - 1), kMinPassingDistance, kMaxPassingDistance);
}

double passingDistance(const JugglingLoop& loop, const JuggleParams& params) {
    if (params.distance > 0.0) return std::clamp(params.distance, kMinPassingDistance, kMaxPassingDistance);
    return defaultPassingDistance(loop);
}

void jugglerPlacement(const JugglingLoop& loop, const JuggleParams& params, int juggler, Vec3* position,
                      float* yaw) {
    *position = Vec3(0.0f, 0.0f, 0.0f);
    *yaw = 0.0f;
    const int n = loop.jugglers;
    if (n <= 1) return;
    const float d = static_cast<float>(passingDistance(loop, params));
    if (n == 2) {
        // Face to face along X: juggler 1 on the left as the default camera sees it.
        *position = Vec3(juggler == 0 ? -0.5f * d : 0.5f * d, 0.0f, 0.0f);
        *yaw = juggler == 0 ? 0.5f * kPi : -0.5f * kPi;
        return;
    }
    // More jugglers: the corners of a regular polygon, facing the middle, neighbors d apart.
    // J1 is at the front (nearest the default camera, which looks over J1's shoulders), and the
    // numbers go clockwise seen from above: J2 on J1's left, the last juggler on J1's right.
    const float radius = 0.5f * d / std::sin(kPi / static_cast<float>(n));
    const float angle = 2.0f * kPi * static_cast<float>(juggler) / static_cast<float>(n);
    *position = Vec3(-radius * std::sin(angle), 0.0f, radius * std::cos(angle));
    *yaw = kPi - angle;  // facing the middle
}

int choreographyCycle(const JugglingLoop& loop, const JuggleParams& params) {
    const Choreography* c = params.choreography;
    if (!c || !c->active() || loop.period <= 0) return 0;
    if (c->cycle > 0) return c->cycle;
    // A whole number of the pattern's periods, enough to hold every keyframe (a choreography
    // can take longer than the pattern to come round).
    int lastBeat = 0;
    for (const std::vector<Keyframe>& keys : c->keys)
        for (const Keyframe& k : keys) lastBeat = std::max(lastBeat, k.beat);
    return (lastBeat / loop.period + 1) * loop.period;
}

void jugglerPlacementAt(const JugglingLoop& loop, const JuggleParams& params, int juggler, double beat,
                        Vec3* position, float* yaw) {
    const Choreography* c = params.choreography;
    const int cycle = choreographyCycle(loop, params);
    if (cycle > 0) {
        const float scale = c->reference > 0.0 ? static_cast<float>(passingDistance(loop, params) / c->reference) : 1.0f;
        if (choreographyPlacement(*c, juggler, beat, cycle, scale, position, yaw)) return;
        if (juggler >= 0 && juggler < static_cast<int>(c->marks.size())) {  // on their own mark
            const SpikeMark& m = c->marks[static_cast<size_t>(juggler)];
            *position = Vec3(m.x * scale, 0.0f, m.z * scale);
            *yaw = m.yaw;
            return;
        }
    }
    jugglerPlacement(loop, params, juggler, position, yaw);
}

SceneExtents computeSceneExtents(const JugglingLoop& sketchOrLoop, const JuggleParams& params, int onlyJuggler) {
    SceneExtents e;
    if (sketchOrLoop.empty()) return e;
    const JugglingLoop loop = physicsLoop(sketchOrLoop);
    Evaluator ev(loop, params, patternIntensity(loop, params));
    ev.useQuickBody();
    const int span = ev.span();  // hands, throws (and any walking) all repeat
    const float handThickness = 0.025f;
    e.lowestHandY = 1e9f;
    e.highestPropY = -1e9f;
    float minX = 1e9f, maxX = -1e9f, sumZ = 0.0f;
    int countZ = 0;
    e.nearZ = -1e9f;
    auto includeX = [&](float x, float margin) {
        minX = std::min(minX, x - margin);
        maxX = std::max(maxX, x + margin);
    };
    const float reach = propReach(params.prop);
    for (int j = 0; j < loop.jugglers; ++j) {
        if (onlyJuggler >= 0 && j != onlyJuggler) continue;
        for (int right = 0; right < 2; ++right) {
            sumZ += ev.placement(j, 0.0).point(ev.throwPointAt(j, right ? 0 : 1)).z;
            ++countZ;
        }
        // The body (shoulders, back and chest), which matters when jugglers face each other:
        // seen from the side, their backs are the outermost things. (On every beat, in case
        // they walk.)
        const Vec3 bodyPoints[] = {{0.25f, 0.0f, 0.0f}, {-0.25f, 0.0f, 0.0f}, {0.0f, 0.0f, -0.2f}, {0.0f, 0.0f, 0.15f}};
        for (int b = 0; b < span; ++b) {
            const Placement pl = ev.placement(j, b);
            for (const Vec3& p : bodyPoints) {
                includeX(pl.point(p).x, 0.05f);
                e.nearZ = std::max(e.nearZ, pl.point(p).z + 0.05f);
            }
        }
        // Hands, and what they hold: sample their paths over one full repeat, 24 times a beat
        // (the paths are smooth, so that's within a few millimeters, and it's slow for long
        // passing patterns: every sample works out the body's motion). A hand that's empty at
        // the moment still gets a "held" prop here; that only makes the framing a bit roomier.
        // When the repeat is long (walking that takes a long time to line up with the throws),
        // fewer samples a beat, at most kMaxSamples in all: the framing only needs to be close.
        constexpr int kSamplesPerBeat = 24, kMaxSamples = 4096;
        const int samples = std::min(span * kSamplesPerBeat, kMaxSamples);
        for (int i = 0; i <= samples; ++i) {
            const double t = static_cast<double>(span) * i / samples;
            const Placement pl = ev.placement(j, t);
            for (int right = 0; right < 2; ++right) {
                const Vec3 p = pl.point(ev.palm(j, right != 0, t));
                e.lowestHandY = std::min(e.lowestHandY, p.y - handThickness);
                includeX(p.x, 0.06f);
                if (params.prop != PropType::Ball) {
                    BallState held;
                    ev.heldProp(j, right != 0, t, &held);
                    // The near end of a club (the knob) or the bottom of a ring.
                    const float low = params.prop == PropType::Club
                                          ? (held.center - held.axis * (kClubLength - kClubCenterOfMass)).y
                                          : held.center.y - kRingOuterRadius;
                    e.lowestHandY = std::min(e.lowestHandY, low);
                    e.highestPropY = std::max(e.highestPropY, held.center.y + reach);
                    includeX(held.center.x, reach);
                } else {
                    e.highestPropY = std::max(e.highestPropY, p.y + 0.0125f + 2.0f * kBallRadius);
                }
            }
        }
        // Flights: the top of each is at the vertex of its parabola (or an endpoint).
        for (int b = 0; b < span; ++b) {
            const int v = ev.valueAt(j, b);
            if (v <= 0 || ev.isHeld(j, b)) continue;  // empty, or held (covered by the hands above)
            if (onlyJuggler >= 0 && ev.destAt(j, b) != j) continue;  // a pass: framing one juggler
            const double catchTime = ev.catchTimeFor(ev.destAt(j, b), b + v);
            const int kSamples = std::clamp(4096 / span, 4, 64);  // fewer in a long repeat
            for (int i = 0; i <= kSamples; ++i) {
                const Vec3 p = ev.flightPosition(j, b, v, b + (catchTime - b) * i / kSamples);
                e.highestPropY = std::max(e.highestPropY, p.y + reach);
                includeX(p.x, reach);
            }
        }
    }
    if (e.highestPropY < e.lowestHandY) e.highestPropY = e.lowestHandY + 0.2f;
    e.centerX = 0.5f * (minX + maxX);
    e.halfWidth = 0.5f * (maxX - minX);
    e.centerZ = countZ > 0 ? sumZ / static_cast<float>(countZ) : 0.0f;
    e.nearZ = std::max(e.nearZ, e.centerZ);
    e.ring = loop.jugglers >= 3 && onlyJuggler < 0;
    return e;
}

std::vector<ThrowSpin> throwSpins(const JugglingLoop& loop, const JuggleParams& params) {
    std::vector<ThrowSpin> out;
    if (loop.empty() || params.prop == PropType::Ball) return out;
    for (const LoopThrow& t : loop.throws)
        if (t.value == kOpenThrow) return out;
    const Evaluator ev(loop, params, patternIntensity(loop, params));
    out.resize(static_cast<size_t>(loop.jugglers * loop.period));
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = 0; b < loop.period; ++b) {
            const int v = ev.valueAt(j, b);
            if (v <= 0 || ev.isHeld(j, b)) continue;
            ThrowSpin& s = out[static_cast<size_t>(j * loop.period + b)];
            s.spins = ev.spins(j, b, v);
            s.flightBeats = static_cast<float>(ev.catchTimeFor(ev.destAt(j, b), b + v) - b);
        }
    }
    return out;
}

JugglerScene evaluateScene(const JugglingLoop& sketchOrLoop, const BallOrbits& orbits, const JuggleParams& params,
                           double t) {
    JugglerScene scene;
    if (sketchOrLoop.empty()) return scene;
    const JugglingLoop loop = physicsLoop(sketchOrLoop);
    const bool sketch = loop != sketchOrLoop;
    const LoopOrbits paths = sketch ? computeLoopOrbits(sketchOrLoop) : LoopOrbits();
    const Evaluator ev(loop, params, patternIntensity(loop, params));
    // Props: their orbit numbering, or in a sketch, their path.
    auto ballOf = [&](int j, int b) {
        return sketch ? std::max(0, paths.idAt(j, b)) : orbitBallAt(orbits, j, b);
    };
    auto isOpen = [&](int j, int b) { return sketch && sketchOrLoop.at(j, b).value == kOpenThrow; };
    scene.prop = params.prop;
    for (int j = 0; j < loop.jugglers; ++j) {
        const Placement pl = ev.placement(j, t);
        JugglerState js;
        js.position = pl.position;
        js.yaw = pl.yaw;
        js.palmRight = ev.palm(j, true, t);
        js.palmLeft = ev.palm(j, false, t);
        js.body = ev.bodyAt(j, t);
        js.body.lookAt = pl.localPoint(ev.gazeAt(j, t * ev.secondsPerBeat()));
        scene.jugglers.push_back(js);
    }

    // Every ball is, at any moment, in exactly one segment: in flight after a throw, held before
    // its next throw, or riding a 2. Look at throws within reach of t (values are at most 35).
    const int now = static_cast<int>(std::floor(t));
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = now - 36; b <= now + 36; ++b) {
            const int v = ev.valueAt(j, b);
            const bool right = loop.rightHandBeat(j, b);
            if (isOpen(j, b) && ev.incomingAt(j, b) > 0) {
                // A sketch's undecided throw: the prop landing here is caught and held as usual,
                // then vanishes when it would have been thrown.
                const int vIn = ev.incomingAt(j, b);
                const int src = ev.incomingFrom(j, b);
                const int ball = ballOf(src, b - vIn);
                const double holdStart = b - ev.catchDwell(j, b);
                if (!ev.incomingHeld(j, b) && t >= holdStart && t < b) {  // (a held 2 covers its own hold)
                    BallState held;
                    held.ball = ball;
                    ev.heldProp(j, right, t, &held);
                    scene.balls.push_back(held);
                }
                const double age = (t - b) * ev.secondsPerBeat() / kPuffSeconds;
                if (age >= 0.0 && age < 1.0) {
                    Puff puff;
                    puff.center = ev.placement(j, b).point(ev.palm(j, right, static_cast<double>(b)));
                    puff.age = static_cast<float>(age);
                    puff.ball = ball;
                    puff.appearing = false;
                    scene.puffs.push_back(puff);
                }
                continue;
            }
            if (v <= 0) continue;
            const int ball = ballOf(j, b);
            // In a sketch, a prop thrown from a spot nothing lands in pops into the hand when it
            // would have been caught there.
            if (sketch && ev.incomingAt(j, b) == 0) {
                const double appearAt = b - ev.catchDwell(j, b);
                const double age = (t - appearAt) * ev.secondsPerBeat() / kPuffSeconds;
                if (age >= 0.0 && age < 1.0) {
                    Puff puff;
                    puff.center = ev.placement(j, appearAt).point(ev.palm(j, right, appearAt));
                    puff.age = static_cast<float>(age);
                    puff.ball = ball;
                    puff.appearing = true;
                    scene.puffs.push_back(puff);
                }
            }

            // Held before this throw (unless it arrived as a held 2, which covers its own hold).
            if (!ev.incomingHeld(j, b)) {
                const double holdStart = b - ev.catchDwell(j, b);
                if (t >= holdStart && t < b) {
                    BallState held;
                    held.ball = ball;
                    ev.heldProp(j, right, t, &held);
                    scene.balls.push_back(held);
                }
            }

            if (ev.isHeld(j, b)) {
                // A held 2 stays in the hand until it's thrown again two beats later.
                if (t >= b && t < b + 2) {
                    BallState held;
                    held.ball = ball;
                    held.throwValue = v;
                    held.thrower = held.catcher = j;
                    ev.heldProp(j, right, t, &held);
                    scene.balls.push_back(held);
                }
                continue;
            }

            // In flight, and the trail it leaves.
            const double catchTime = ev.catchTimeFor(ev.destAt(j, b), b + v);
            if (t >= b && t < catchTime) {
                BallState flying;
                flying.ball = ball;
                flying.inFlight = true;
                flying.throwValue = v;
                flying.thrower = j;
                flying.catcher = ev.destAt(j, b);
                flying.spins = ev.spins(j, b, v);
                flying.flightBeats = static_cast<float>(catchTime - b);
                ev.flightProp(j, b, v, t, &flying);
                scene.balls.push_back(flying);
            }
            // Half the flight long, but at most kMaxTrailBeats: a hand throws at most every
            // other beat, so the next prop on the same path (in a 5, a 7, 373737, ...) is at
            // least two beats behind, and its trail shouldn't run into the end of this one.
            const double trailLength = std::min(0.5 * (catchTime - b), kMaxTrailBeats);
            const double from = std::max(static_cast<double>(b), t - trailLength);
            const double to = std::min(t, catchTime);
            if (t >= b && to > from) {
                Trail trail;
                trail.ball = ball;
                const int kSamples = 24;
                for (int i = 0; i <= kSamples; ++i) {
                    const double s = from + (to - from) * i / kSamples;
                    trail.points.push_back(ev.flightPosition(j, b, v, s));
                    trail.fade.push_back(static_cast<float>(1.0 - (t - s) / trailLength));
                }
                scene.trails.push_back(trail);
            }
        }
    }
    return scene;
}

float patternIntensity(const JugglingLoop& sketchOrLoop, const JuggleParams& params) {
    if (sketchOrLoop.empty()) return 0.0f;
    const JugglingLoop loop = physicsLoop(sketchOrLoop);
    const Evaluator ev(loop, params);
    // Heights of the thrown throws (empty beats and held 2s don't count).
    float maxFraction = 0.0f, sum = 0.0f, sumSq = 0.0f;
    int count = 0;
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = 0; b < loop.period; ++b) {
            const int v = ev.valueAt(j, b);
            if (v <= 0 || ev.isHeld(j, b)) continue;
            const float f = ev.heightFraction(j, b);
            maxFraction = std::max(maxFraction, f);
            sum += f;
            sumSq += f * f;
            ++count;
        }
    }
    if (count == 0) return 0.0f;
    const float n = static_cast<float>(count);
    const float mean = sum / n;
    const float spread = std::sqrt(std::max(0.0f, sumSq / n - mean * mean));
    return std::clamp(0.6f * maxFraction + 0.8f * spread, 0.0f, 1.0f);
}

const char* propTypeName(PropType prop) {
    switch (prop) {
        case PropType::Club: return "Clubs";
        case PropType::Ring: return "Rings";
        default: return "Balls";
    }
}

const char* propTypeKey(PropType prop) {
    switch (prop) {
        case PropType::Club: return "clubs";
        case PropType::Ring: return "rings";
        default: return "balls";
    }
}

bool propTypeFromKey(const char* key, PropType* prop) {
    for (int i = 0; i < static_cast<int>(PropType::Count); ++i) {
        const PropType p = static_cast<PropType>(i);
        if (std::string(key) == propTypeKey(p)) {
            *prop = p;
            return true;
        }
    }
    return false;
}
