// juggle_sim.cpp - see juggle_sim.h.
#include "juggle_sim.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr float kGravity = 9.81f;  // m/s^2, along -Y

int modPositive(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

bool isRightHandBeat(int beat) { return modPositive(beat, 2) == 0; }

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
constexpr float kGazeSmoothing = 0.15f;     // s: and the head's gaze over this long
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

class Evaluator {
public:
    Evaluator(const std::vector<int>& loop, const JuggleParams& params, float intensity = 0.0f)
        : loop_(loop), period_(static_cast<int>(loop.size())), params_(params), intensity_(intensity) {
        // The value of the throw landing on each slot (0 if only an empty beat "lands" there).
        incoming_.assign(loop.size(), 0);
        for (int s = 0; s < period_; ++s) {
            const int v = loop[static_cast<size_t>(s)];
            if (v > 0) incoming_[static_cast<size_t>(modPositive(s + v, period_))] = v;
        }
        // Per-throw data used many times a frame by the body and gaze, for one full repeat of
        // the pattern (two loops if the loop is odd, so each beat is seen from both hands).
        span_ = period_ % 2 == 0 ? period_ : 2 * period_;
        throws_.resize(static_cast<size_t>(span_));
        for (int b = 0; b < span_; ++b) {
            ThrowInfo& info = throws_[static_cast<size_t>(b)];
            info.value = valueAt(b);
            info.catchDwell = catchDwell(b);
            info.drive = driveFor(b);
            const float hf = heightFraction(b);
            const float bodyDrive = std::max(0.0f, info.drive.distance - kMaxArmDrive);
            // A body can only sink and rise so fast: with too little time (a short dwell at a
            // slow tempo), it dips less.
            const float bumpSeconds =
                std::min(info.drive.carrySeconds - info.drive.seconds, 1.6f * info.drive.seconds);
            const float maxDip = kMaxBodySpeed * std::max(bumpSeconds, 0.0f);
            info.dip = std::min(kCrouchPerBodyDrive * bodyDrive + kFervorBob * intensity_ * hf, maxDip);
            info.sway = std::min(kMaxSway * hf * (0.4f + 0.6f * intensity_), 0.4f * maxDip);
            if (info.value > 0 && info.value != 2) {
                info.launch = launchPoint(b);
                info.launchVelocity = launchVelocity(b, info.value);
                info.flightBeats = static_cast<float>(catchTimeFor(b + info.value) - b);
            }
        }
    }

    int valueAt(int beat) const { return loop_[static_cast<size_t>(modPositive(beat, period_))]; }
    int incomingAt(int beat) const {
        return incoming_[static_cast<size_t>(modPositive(beat, period_))];
    }

    // Dwell (beats) of the catch made before throwing on `beat`.
    double catchDwell(int beat) const {
        const int vIn = incomingAt(beat);
        if (vIn == 0) return params_.dwellBeats;               // empty: the hand still scoops
        if (vIn == 2) return std::min(params_.dwellBeats, 1.0);  // a held 2: same rhythm
        return std::min(params_.dwellBeats, 0.5 * vIn);
    }

    double secondsPerBeat() const { return 60.0 / params_.bpm; }
    double catchTimeFor(int beat) const { return beat - catchDwell(beat); }

    // How high the throw made on beat b goes above the hands, as a fraction of the human
    // limit (0 for empty beats and 2s, which aren't thrown). Depends only on timing.
    float heightFraction(int b) const {
        const int v = valueAt(b);
        if (v <= 0 || v == 2) return 0.0f;
        const float T = flightSeconds(b, v);
        const float height = kGravity * T * T / 8.0f;
        return std::clamp(height / kHumanLimitHeight, 0.0f, 1.0f);
    }
    // Same, for the throw caught before throwing on `beat`.
    float incomingHeightFraction(int beat) const {
        const int vIn = incomingAt(beat);
        return vIn > 0 ? heightFraction(beat - vIn) : 0.0f;
    }

    // Where the hand releases on `beat` and catches before throwing on `beat`.
    Vec3 throwPointAt(int beat) const { return throwPoint(isRightHandBeat(beat), heightFraction(beat)); }
    Vec3 catchPointAt(int beat) const {
        return catchPoint(isRightHandBeat(beat), incomingHeightFraction(beat));
    }

    // ---- Props: how each throw's prop is turned and how fast it spins.
    PropType prop() const { return params_.prop; }
    float gripToCenter() const {
        return prop() == PropType::Club ? kClubGripToCenter
                                        : (prop() == PropType::Ring ? kRingGripToCenter : 0.0f);
    }
    // The plane the throw made on beat b spins in: clubs and rings both spin end over end,
    // in the plane running front to back through the juggler (turned by kClubAxisBias), the
    // top turning back toward the juggler.
    SpinFrame frameFor(int b) const {
        const bool right = isRightHandBeat(b);
        SpinFrame f;
        if (prop() != PropType::Ball) {
            const float side = std::sin(kClubAxisBias);
            f.dir = Vec3(right ? -side : side, 0.0f, -std::cos(kClubAxisBias));
        }
        return f;
    }
    // Center of the prop relative to the hand (palm) holding it.
    Vec3 propOffset(const SpinFrame& f, float theta) const {
        if (prop() == PropType::Ball) return kBallOnPalm;
        return f.at(theta) * gripToCenter();
    }
    // Whole turns the throw on beat b (value v) makes in the air: about kNaturalSpinRate turns
    // a second of flight, at least one. 1s go across flat (no turn); 2s are held.
    int spins(int b, int v) const {
        if (prop() == PropType::Ball || v <= 2) return 0;
        const float turns = flightSeconds(b, v) * kNaturalSpinRate;
        return std::max(1, static_cast<int>(std::lround(turns)));
    }
    // Spin rate in the air (radians/second): the spins plus getting from the release angle to
    // the catch angle, over exactly the flight time.
    float spinRate(int b, int v) const {
        if (prop() == PropType::Ball || v <= 0 || v == 2) return 0.0f;
        const PropAngles a = propAngles(prop());
        const float turn = 2.0f * kPi * static_cast<float>(spins(b, v)) + a.caught - a.release;
        return turn / flightSeconds(b, v);
    }

    // ---- Flights. The prop's center of mass flies on the parabola. Its endpoints are where
    // the center is when the hand is at the throw point (release) and at the catch point
    // (catch), so flights don't depend on the hand path.
    Vec3 launchPoint(int b) const {
        return throwPointAt(b) + propOffset(frameFor(b), propAngles(prop()).release);
    }
    Vec3 landingPoint(int b, int v) const {
        return catchPointAt(b + v) + propOffset(frameFor(b), propAngles(prop()).caught);
    }
    float flightSeconds(int b, int v) const {
        return static_cast<float>((catchTimeFor(b + v) - b) * secondsPerBeat());
    }
    // Launch velocity: chosen so the parabola from the launch point reaches the landing point
    // exactly when the catch is due.
    Vec3 launchVelocity(int b, int v) const {
        const float T = flightSeconds(b, v);
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        return (landingPoint(b, v) - launchPoint(b)) * (1.0f / T) - accel * (0.5f * T);
    }
    // Position of a prop's center in flight: thrown on beat b (value v), at time t.
    Vec3 flightPosition(int b, int v, double t) const {
        const float tau = static_cast<float>((t - b) * secondsPerBeat());
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        return launchPoint(b) + launchVelocity(b, v) * tau + accel * (0.5f * tau * tau);
    }
    // The whole prop in flight: center, and the turn it has made since the release.
    void flightProp(int b, int v, double t, BallState* out) const {
        out->prop = prop();
        out->center = flightPosition(b, v, t);
        const SpinFrame f = frameFor(b);
        const float tau = static_cast<float>((t - b) * secondsPerBeat());
        const float theta = propAngles(prop()).release + spinRate(b, v) * tau;
        out->axis = f.at(theta);
        out->spinAxis = f.spinAxis();
    }

    // Velocity the hand has when it releases on `beat`: whatever makes the prop's center leave
    // at its launch velocity. For a ball that's the launch velocity itself; a club or ring is
    // also turning about the hand, which carries its center along, so the hand moves that much
    // less. Rest for a 2 (held) or an empty beat.
    Vec3 releaseVelocity(int beat) const {
        const int v = valueAt(beat);
        if (v <= 0 || v == 2) return Vec3(0.0f, 0.0f, 0.0f);
        const float turning = spinRate(beat, v) * gripToCenter();
        return launchVelocity(beat, v) -
               frameFor(beat).rate(propAngles(prop()).release) * turning;
    }
    // Velocity the hand has at the catch before throwing on `beat`: a fraction of the incoming
    // prop's velocity, so the hand "gives" with the catch (an inelastic collision).
    Vec3 catchVelocity(int beat) const {
        const int vIn = incomingAt(beat);
        if (vIn <= 0 || vIn == 2) return Vec3(0.0f, 0.0f, 0.0f);
        const int from = beat - vIn;
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        const Vec3 centerVelocity = launchVelocity(from, vIn) + accel * flightSeconds(from, vIn);
        const float turning = spinRate(from, vIn) * gripToCenter();
        const Vec3 gripVelocity =
            centerVelocity - frameFor(from).rate(propAngles(prop()).caught) * turning;
        return gripVelocity * kCatchSoftness;
    }

    // The drive for the release on `beat`: v^2 / 2a at the hand's top acceleration, up to the
    // longest drive arms and body can make together. Past that (throws above the human limit)
    // the hand still reaches the release speed, just with more than human acceleration.
    // The speed is the prop's launch speed: for a club or ring the wrist's flick adds some of
    // that at the release, but the arm still drives the prop's whole mass up to speed.
    Drive driveFor(int beat) const {
        Drive d;
        const int v = valueAt(beat);
        const float speed = (v > 0 && v != 2) ? length(launchVelocity(beat, v)) : 0.0f;
        d.carrySeconds = static_cast<float>(catchDwell(beat) * secondsPerBeat());
        d.distance = std::clamp(speed * speed / (2.0f * kHandDriveAccel), kMinScoop, kMaxDrive);
        d.seconds = speed > 0.05f ? 2.0f * d.distance / speed : 0.5f * d.carrySeconds;
        d.seconds = std::min(d.seconds, 0.7f * d.carrySeconds);
        return d;
    }

    // ---- Body. Each throw on beat n contributes a smooth bump in time: it starts at the catch,
    // peaks where the hand's drive begins, and dies away just after the release (the body is
    // still rising as the ball leaves). The crouch is the deepest of the bumps going on (so a
    // run of high throws keeps the juggler low, bobbing on each), and the sway adds them up,
    // alternating sides with the hands.
    //
    // Returns the bump's shape (0..1) at time t (seconds) for the throw on beat n.
    float bumpAt(int n, double tSeconds) const {
        const ThrowInfo& info = throwInfo(n);
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
    void rawBody(double tSeconds, float* dip, float* sway) const {
        const double spb = secondsPerBeat();
        const int now = static_cast<int>(std::floor(tSeconds / spb));
        float dip4 = 0.0f;
        *sway = 0.0f;
        // A throw's bump lasts from its catch (under 2 beats before it) to just after it.
        for (int n = now - 1; n <= now + 2; ++n) {
            const float shape = bumpAt(n, tSeconds);
            if (shape <= 0.0f) continue;
            const ThrowInfo& info = throwInfo(n);
            const float d = shape * info.dip;
            dip4 += (d * d) * (d * d);
            // The two hands' bumps overlap (one hand scoops while the other is still holding),
            // so the sway uses a sharpened bump that's only large around this hand's drive;
            // otherwise the sides would cancel out.
            const float side = isRightHandBeat(n) ? -1.0f : 1.0f;
            *sway += side * shape * shape * shape * info.sway;
        }
        *dip = std::sqrt(std::sqrt(dip4));
    }

    // The body at beat t. The targets above say where each throw wants the body; a real body
    // has mass and can't follow them instantly (the hips can't even drop faster than gravity),
    // so they're smoothed over time (a Gaussian average over about kBodySmoothing either side).
    // Quick changes, like a deep dip for every beat of a fast high pattern, come out as a
    // steadier crouch with a small bob, much as they do for a real juggler. Smoothing looks
    // ahead as well as back, so the body anticipates a big throw a little, as people do.
    BodyMotion bodyAt(double t) const {
        BodyMotion m;
        m.intensity = intensity_;
        const double tSeconds = t * secondsPerBeat();
        const int kTaps = 20;  // each side
        float dip = 0.0f, sway = 0.0f, weightSum = 0.0f;
        for (int k = -kTaps; k <= kTaps; ++k) {
            const float u = 3.0f * static_cast<float>(k) / kTaps;  // in sigmas
            const float w = std::exp(-0.5f * u * u);
            float d = 0.0f, s = 0.0f;
            rawBody(tSeconds + u * kBodySmoothing, &d, &s);
            dip += w * d;
            sway += w * s;
            weightSum += w;
        }
        dip /= weightSum;
        sway /= weightSum;
        const float crouch = kBaseCrouch + kFervorCrouch * intensity_ + dip;
        m.pelvisOffset = Vec3(kSwayHipShare * sway, -crouch, -kHipsBack * crouch);
        m.lean = kLeanPerCrouch * crouch + kFervorLean * intensity_;
        m.roll = std::atan2((1.0f - kSwayHipShare) * sway, kTorsoHeight);
        return m;  // lookAt is filled in separately (gazeAt): the hands don't need it
    }

    // Where the head looks: mostly at the highest balls. A weighted average of the balls in
    // flight, weighting each by (height above the hands)^4, plus a resting point in front of
    // the hands. The weights go by how heights compare, not by meters, so two high balls
    // passing each other hand the gaze over just as gently as two low ones. A head has mass
    // too, so this is smoothed over time like the body.
    Vec3 rawGaze(double tSeconds) const {
        auto gazeWeight = [](float heightAboveHands) {
            const float h = std::max(heightAboveHands, 0.0f) + 0.05f;
            return (h * h) * (h * h);
        };
        const double spb = secondsPerBeat();
        const double t = tSeconds / spb;
        const Vec3 rest(0.0f, 1.2f, 0.5f);
        Vec3 sum = rest * gazeWeight(0.25f);
        float weightSum = gazeWeight(0.25f);
        const int now = static_cast<int>(std::floor(t));
        for (int b = now - 36; b <= now; ++b) {  // flights last under 36 beats
            const ThrowInfo& info = throwInfo(b);
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
        return sum * (1.0f / weightSum);
    }
    Vec3 gazeAt(double tSeconds) const {
        const int kTaps = 12;  // each side
        Vec3 sum(0.0f, 0.0f, 0.0f);
        float weightSum = 0.0f;
        for (int k = -kTaps; k <= kTaps; ++k) {
            const float u = 3.0f * static_cast<float>(k) / kTaps;
            const float w = std::exp(-0.5f * u * u);
            sum = sum + rawGaze(tSeconds + u * kGazeSmoothing) * w;
            weightSum += w;
        }
        return sum * (1.0f / weightSum);
    }
    Vec3 shoulderAt(bool right, double t) const { return bodyPoint(bodyAt(t), neutralShoulder(right)); }

    // ---- Hands. Each hand's cycle, from one of its releases (prev) to the next (next):
    //   1. follow-through: leaves the throw point at the release velocity and decelerates to a
    //      stop a short way along it;
    //   2. return: swings out to the catch point, arriving at the catch velocity;
    //   3. scoop: gives with the ball down to a low point, at rest;
    //   4. drive: accelerates up to the throw point, reaching the next release velocity.
    // The scoop's depth is the drive (see driveFor), so higher throws scoop deeper; the body
    // crouches for the part of it the arms can't do. Every segment is a Hermite curve, so
    // position and velocity are continuous all the way round.
    Vec3 palm(bool right, double t) const {
        int prev = static_cast<int>(std::floor(t));
        if (isRightHandBeat(prev) != right) --prev;
        const int next = prev + 2;
        const double spb = secondsPerBeat();
        const double catchTime = catchTimeFor(next);
        const Vec3 tpPrev = throwPointAt(prev), tp = throwPointAt(next), cp = catchPointAt(next);
        const Vec3 zero(0.0f, 0.0f, 0.0f);

        // 1. Follow-through after the release on `prev`.
        const Vec3 vOut = releaseVelocity(prev);
        const float emptySeconds = static_cast<float>((catchTime - prev) * spb);
        const float outSpeed = length(vOut);
        float followDist = std::min(outSpeed * outSpeed / (2.0f * kHandStopAccel), kMaxFollowThrough);
        float followSeconds = outSpeed > 1e-3f ? 2.0f * followDist / outSpeed : 0.0f;
        if (followSeconds > 0.45f * emptySeconds) {  // not enough time: shorten it
            followSeconds = 0.45f * emptySeconds;
            followDist = 0.5f * outSpeed * followSeconds;
        }
        const Vec3 followPoint = withinReach(shoulderAt(right, prev + followSeconds / spb),
                                             tpPrev + normalize(vOut) * followDist);
        const float returnSeconds = emptySeconds - followSeconds;

        // 3-4. Scoop down to the low point, then drive up to the release on `next`.
        const Vec3 vIn = releaseVelocity(next);
        const Drive drive = driveFor(next);
        const float driveSeconds = drive.seconds;
        // The low point is back along the direction of the throw (straight down for a vertical
        // one), pulled in toward the body if that's out of reach (with the body where it is
        // at that moment).
        const Vec3 driveDir = length(vIn) > 0.05f ? normalize(vIn) : Vec3(0.0f, 1.0f, 0.0f);
        const double lowTime = next - driveSeconds / spb;
        const Vec3 lowPoint = withinReach(shoulderAt(right, lowTime), tp - driveDir * drive.distance);
        const float scoopSeconds = drive.carrySeconds - driveSeconds;

        // 2/3. The hand's velocity at the catch: it gives with the ball, but no faster than a
        // hand plausibly moves, and no faster than the segments on either side can absorb
        // without swinging wide (a Hermite segment overshoots if its end speed is much more than
        // its average speed).
        Vec3 vCatch = catchVelocity(next);
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

    // The prop a hand holds at time t (if it holds one): from the catch, the prop's turn
    // carries on a little (the hand gives with it), swings back to the cocked angle by the
    // bottom of the scoop, and is flicked forward through the drive so it leaves the hand at
    // the release angle with exactly the next throw's spin. Between releases, only a held 2
    // is still in the hand, resting at the release angle. The plane it turns in moves over
    // from the throwing hand's to this hand's during the carry.
    void heldProp(bool right, double t, BallState* out) const {
        out->prop = prop();
        const Vec3 p = palm(right, t);
        int prev = static_cast<int>(std::floor(t));
        if (isRightHandBeat(prev) != right) --prev;
        const int next = prev + 2;
        const PropAngles a = propAngles(prop());
        SpinFrame frame;
        float theta = a.release;
        const double catchTime = catchTimeFor(next);
        if (t < catchTime) {
            frame = frameFor(prev);
        } else {
            const int vIn = incomingAt(next);
            SpinFrame from = frameFor(next);
            float startAngle = a.release, startRate = 0.0f;
            if (vIn == 2) {
                from = frameFor(next - 2);
            } else if (vIn > 0) {
                from = frameFor(next - vIn);
                startAngle = a.caught;
                startRate = kCatchSoftness * spinRate(next - vIn, vIn);
            }
            const SpinFrame to = frameFor(next);
            const Drive drive = driveFor(next);
            // The flick (cocked to release) takes the end of the carry: the hand's drive, or
            // more if the drive is short (the wrist starts turning the prop up before the hand
            // starts driving).
            const float flickSeconds =
                std::min(std::max(drive.seconds, kFlickShare * drive.carrySeconds), 0.8f * drive.carrySeconds);
            const float dropSeconds = drive.carrySeconds - flickSeconds;
            const float tau = static_cast<float>((t - catchTime) * secondsPerBeat());
            const int v = valueAt(next);
            const float endRate = (v > 0 && v != 2) ? spinRate(next, v) : 0.0f;
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
        out->center = p + propOffset(frame, theta);
        out->axis = frame.at(theta);
        out->spinAxis = frame.spinAxis();
    }

private:
    const std::vector<int>& loop_;
    int period_;
    JuggleParams params_;
    struct ThrowInfo {
        int value = 0;
        double catchDwell = 0.0;
        Drive drive;
        float dip = 0.0f;    // how far this throw wants the hips to dip
        float sway = 0.0f;   // how far it wants the torso to sway toward its hand
        Vec3 launch, launchVelocity;
        float flightBeats = 0.0f;  // 0 if not thrown (empty beat or a held 2)
    };
    const ThrowInfo& throwInfo(int beat) const {
        return throws_[static_cast<size_t>(modPositive(beat, span_))];
    }

    float intensity_;
    std::vector<int> incoming_;
    int span_ = 0;
    std::vector<ThrowInfo> throws_;
};

}  // namespace

SceneExtents computeSceneExtents(const std::vector<int>& loop, const JuggleParams& params) {
    SceneExtents e;
    if (loop.empty()) return e;
    const Evaluator ev(loop, params, patternIntensity(loop, params));
    const int period = static_cast<int>(loop.size());
    const int span = period % 2 == 0 ? period : period * 2;  // hands and throws both repeat
    const float handThickness = 0.025f;
    e.lowestHandY = 1e9f;
    e.highestPropY = -1e9f;
    const float reach = propReach(params.prop);
    // Hands, and what they hold: sample their paths over one full repeat. (A hand that's empty
    // at the moment still gets a "held" prop here; that only makes the framing a bit roomier.)
    for (int i = 0; i <= span * 100; ++i) {
        const double t = i / 100.0;
        for (int right = 0; right < 2; ++right) {
            const Vec3 p = ev.palm(right != 0, t);
            e.lowestHandY = std::min(e.lowestHandY, p.y - handThickness);
            e.halfWidth = std::max(e.halfWidth, std::fabs(p.x) + 0.06f);
            if (params.prop != PropType::Ball) {
                BallState held;
                ev.heldProp(right != 0, t, &held);
                // The near end of a club (the knob) or the bottom of a ring.
                const float low = params.prop == PropType::Club
                                      ? (held.center - held.axis * (kClubLength - kClubCenterOfMass)).y
                                      : held.center.y - kRingOuterRadius;
                e.lowestHandY = std::min(e.lowestHandY, low);
                e.highestPropY = std::max(e.highestPropY, held.center.y + reach);
                e.halfWidth = std::max(e.halfWidth, std::fabs(held.center.x) + reach);
            } else {
                e.highestPropY = std::max(e.highestPropY, p.y + 0.0125f + 2.0f * kBallRadius);
            }
        }
    }
    // Flights: the top of each is at the vertex of its parabola (or an endpoint).
    for (int b = 0; b < span; ++b) {
        const int v = ev.valueAt(b);
        if (v <= 0 || v == 2) continue;  // empty, or held (covered by the hands above)
        const double catchTime = b + v - ev.catchDwell(b + v);
        const int kSamples = 64;
        for (int i = 0; i <= kSamples; ++i) {
            const Vec3 p = ev.flightPosition(b, v, b + (catchTime - b) * i / kSamples);
            e.highestPropY = std::max(e.highestPropY, p.y + reach);
            e.halfWidth = std::max(e.halfWidth, std::fabs(p.x) + reach);
        }
    }
    if (e.highestPropY < e.lowestHandY) e.highestPropY = e.lowestHandY + 0.2f;
    return e;
}

JugglerScene evaluateScene(const std::vector<int>& loop, const BallOrbits& orbits,
                           const JuggleParams& params, double t) {
    JugglerScene scene;
    if (loop.empty()) return scene;
    const Evaluator ev(loop, params, patternIntensity(loop, params));
    scene.prop = params.prop;
    scene.palmRight = ev.palm(true, t);
    scene.palmLeft = ev.palm(false, t);
    scene.body = ev.bodyAt(t);
    scene.body.lookAt = ev.gazeAt(t * ev.secondsPerBeat());

    // Every ball is, at any moment, in exactly one segment: in flight after a throw, held before
    // its next throw, or riding a 2. Look at throws within reach of t (values are at most 35).
    const int now = static_cast<int>(std::floor(t));
    for (int b = now - 36; b <= now + 36; ++b) {
        const int v = ev.valueAt(b);
        if (v <= 0) continue;
        const int ball = orbitBallAt(orbits, b);
        const bool right = isRightHandBeat(b);

        // Held before this throw (unless it arrived as a 2, which covers its own hold).
        const int vIn = ev.incomingAt(b);
        if (vIn != 2) {
            const double holdStart = b - ev.catchDwell(b);
            if (t >= holdStart && t < b) {
                BallState held;
                held.ball = ball;
                ev.heldProp(right, t, &held);
                scene.balls.push_back(held);
            }
        }

        if (v == 2) {
            // A 2 stays in the hand until it's thrown again two beats later.
            if (t >= b && t < b + 2) {
                BallState held;
                held.ball = ball;
                ev.heldProp(right, t, &held);
                scene.balls.push_back(held);
            }
            continue;
        }

        // In flight, and the trail it leaves (half the flight long, fading with age).
        const double catchTime = b + v - ev.catchDwell(b + v);
        if (t >= b && t < catchTime) {
            BallState flying;
            flying.ball = ball;
            flying.inFlight = true;
            ev.flightProp(b, v, t, &flying);
            scene.balls.push_back(flying);
        }
        const double trailLength = 0.5 * (catchTime - b);
        const double from = std::max(static_cast<double>(b), t - trailLength);
        const double to = std::min(t, catchTime);
        if (t >= b && to > from) {
            Trail trail;
            trail.ball = ball;
            const int kSamples = 24;
            for (int i = 0; i <= kSamples; ++i) {
                const double s = from + (to - from) * i / kSamples;
                trail.points.push_back(ev.flightPosition(b, v, s));
                trail.fade.push_back(static_cast<float>(1.0 - (t - s) / trailLength));
            }
            scene.trails.push_back(trail);
        }
    }

    return scene;
}

float patternIntensity(const std::vector<int>& loop, const JuggleParams& params) {
    if (loop.empty()) return 0.0f;
    const Evaluator ev(loop, params);
    const int period = static_cast<int>(loop.size());
    // Heights of the thrown throws (empty beats and held 2s don't count).
    float maxFraction = 0.0f, sum = 0.0f, sumSq = 0.0f;
    int count = 0;
    for (int b = 0; b < period; ++b) {
        const int v = ev.valueAt(b);
        if (v <= 0 || v == 2) continue;
        const float f = ev.heightFraction(b);
        maxFraction = std::max(maxFraction, f);
        sum += f;
        sumSq += f * f;
        ++count;
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
