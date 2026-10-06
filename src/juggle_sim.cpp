// juggle_sim.cpp - see juggle_sim.h.
#include "juggle_sim.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kGravity = 9.81f;  // m/s^2, along -Y

int modPositive(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

bool isRightHandBeat(int beat) { return modPositive(beat, 2) == 0; }

// Hand geometry (juggler-local). Throws are made from the inside, catches on the outside.
// Roughly elbow height, in front of the body, so the elbows stay comfortably bent.
Vec3 throwPoint(bool right) { return Vec3(right ? -0.09f : 0.09f, 1.12f, 0.36f); }
Vec3 catchPoint(bool right) { return Vec3(right ? -0.26f : 0.26f, 1.18f, 0.36f); }
const Vec3 kBallOnPalm(0.0f, 0.0125f + kBallRadius, 0.0f);  // ball center relative to palm

// Shoulders and how far a palm can get from its shoulder (upper arm + forearm + a little of the
// hand, minus a margin). Must match the juggler figure's proportions (juggler_figure.cpp).
Vec3 shoulder(bool right) { return Vec3(right ? -0.19f : 0.19f, 1.43f, 0.0f); }
constexpr float kPalmReach = 0.58f;

// Pulls a point that's out of the arm's reach back onto the edge of it, toward the shoulder.
// A deep scoop therefore curves down and in toward the body, as real ones do.
Vec3 withinReach(bool right, Vec3 p) {
    const Vec3 s = shoulder(right);
    const Vec3 d = p - s;
    const float dist = length(d);
    return dist > kPalmReach ? s + d * (kPalmReach / dist) : p;
}

// Hand dynamics (see palm() for how they're used).
constexpr float kHandDriveAccel = 60.0f;   // m/s^2: how hard a hand can accelerate a throw
constexpr float kMaxArmDrive = 0.28f;      // m: deepest scoop the arms alone can make
constexpr float kMinScoop = 0.02f;         // m: even a gentle throw dips a little
constexpr float kHandStopAccel = 120.0f;   // m/s^2: how hard a hand stops after release
constexpr float kMaxFollowThrough = 0.12f; // m
constexpr float kCatchSoftness = 0.4f;     // hand moves at this fraction of the caught ball's velocity
constexpr float kMaxCatchHandSpeed = 2.5f; // m/s: but never faster than this at the catch

// Cubic Hermite: position at time tau (seconds) along a segment of duration T from p0 (velocity
// v0) to p1 (velocity v1). Position and velocity are continuous with neighboring segments.
Vec3 hermite(Vec3 p0, Vec3 v0, Vec3 p1, Vec3 v1, float T, float tau) {
    const float u = T > 1e-6f ? std::clamp(tau / T, 0.0f, 1.0f) : 1.0f;
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u;
    const float h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    return p0 * h00 + v0 * (h10 * T) + p1 * h01 + v1 * (h11 * T);
}

class Evaluator {
public:
    Evaluator(const std::vector<int>& loop, const JuggleParams& params)
        : loop_(loop), period_(static_cast<int>(loop.size())), params_(params) {
        // The value of the throw landing on each slot (0 if only an empty beat "lands" there).
        incoming_.assign(loop.size(), 0);
        for (int s = 0; s < period_; ++s) {
            const int v = loop[static_cast<size_t>(s)];
            if (v > 0) incoming_[static_cast<size_t>(modPositive(s + v, period_))] = v;
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

    // ---- Flights. Endpoints are the fixed throw and catch points (the hand passes through
    // them exactly at release and at the catch), so flights don't depend on the hand path.
    Vec3 launchPoint(int b) const { return throwPoint(isRightHandBeat(b)) + kBallOnPalm; }
    Vec3 landingPoint(int b, int v) const { return catchPoint(isRightHandBeat(b + v)) + kBallOnPalm; }
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
    // Position of a ball in flight: thrown on beat b (value v), at time t (b <= t <= catch).
    Vec3 flightPosition(int b, int v, double t) const {
        const float tau = static_cast<float>((t - b) * secondsPerBeat());
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        return launchPoint(b) + launchVelocity(b, v) * tau + accel * (0.5f * tau * tau);
    }

    // Velocity the hand has when it releases on `beat`: the ball's launch velocity, or rest
    // for a 2 (held) or an empty beat.
    Vec3 releaseVelocity(int beat) const {
        const int v = valueAt(beat);
        if (v <= 0 || v == 2) return Vec3(0.0f, 0.0f, 0.0f);
        return launchVelocity(beat, v);
    }
    // Velocity the hand has at the catch before throwing on `beat`: a fraction of the incoming
    // ball's velocity, so the hand "gives" with the catch (an inelastic hand-ball collision).
    Vec3 catchVelocity(int beat) const {
        const int vIn = incomingAt(beat);
        if (vIn <= 0 || vIn == 2) return Vec3(0.0f, 0.0f, 0.0f);
        const int from = beat - vIn;
        const Vec3 accel(0.0f, -kGravity, 0.0f);
        const Vec3 ballVelocity = launchVelocity(from, vIn) + accel * flightSeconds(from, vIn);
        return ballVelocity * kCatchSoftness;
    }

    // ---- Hands. Each hand's cycle, from one of its releases (prev) to the next (next):
    //   1. follow-through: leaves the throw point at the release velocity and decelerates to a
    //      stop a short way along it;
    //   2. return: swings out to the catch point, arriving at the catch velocity;
    //   3. scoop: gives with the ball down to a low point, at rest;
    //   4. drive: accelerates up to the throw point, reaching the next release velocity.
    // The scoop's depth is the distance needed to reach that speed at kHandDriveAccel
    // (v^2 / 2a), up to what the arms can do; so higher throws scoop deeper. Every segment is a
    // Hermite curve, so position and velocity are continuous all the way round.
    Vec3 palm(bool right, double t) const {
        int prev = static_cast<int>(std::floor(t));
        if (isRightHandBeat(prev) != right) --prev;
        const int next = prev + 2;
        const double spb = secondsPerBeat();
        const double catchTime = catchTimeFor(next);
        const Vec3 tp = throwPoint(right), cp = catchPoint(right);
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
        const Vec3 followPoint = withinReach(right, tp + normalize(vOut) * followDist);
        const float returnSeconds = emptySeconds - followSeconds;

        // 3-4. Scoop down to the low point, then drive up to the release on `next`.
        const Vec3 vIn = releaseVelocity(next);
        const float carrySeconds = static_cast<float>((next - catchTime) * spb);
        const float inSpeed = length(vIn);
        const float drive = std::clamp(inSpeed * inSpeed / (2.0f * kHandDriveAccel), kMinScoop, kMaxArmDrive);
        float driveSeconds = inSpeed > 0.05f ? 2.0f * drive / inSpeed : 0.5f * carrySeconds;
        driveSeconds = std::min(driveSeconds, 0.7f * carrySeconds);
        // The low point is back along the direction of the throw (straight down for a vertical
        // one), pulled in toward the body if that's out of reach.
        const Vec3 driveDir = inSpeed > 0.05f ? normalize(vIn) : Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 lowPoint = withinReach(right, tp - driveDir * drive);
        const float scoopSeconds = carrySeconds - driveSeconds;

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
            if (tau < followSeconds) return hermite(tp, vOut, followPoint, zero, followSeconds, tau);
            return hermite(followPoint, zero, cp, vCatch, returnSeconds, tau - followSeconds);
        }
        const float tau = static_cast<float>((t - catchTime) * spb);
        if (tau < scoopSeconds) return hermite(cp, vCatch, lowPoint, zero, scoopSeconds, tau);
        return hermite(lowPoint, zero, tp, vIn, driveSeconds, tau - scoopSeconds);
    }

    Vec3 ballInHand(bool right, double t) const { return palm(right, t) + kBallOnPalm; }

private:
    const std::vector<int>& loop_;
    int period_;
    JuggleParams params_;
    std::vector<int> incoming_;
};

}  // namespace

SceneExtents computeSceneExtents(const std::vector<int>& loop, const JuggleParams& params) {
    SceneExtents e;
    if (loop.empty()) return e;
    const Evaluator ev(loop, params);
    const int period = static_cast<int>(loop.size());
    const int span = period % 2 == 0 ? period : period * 2;  // hands and throws both repeat
    const float handThickness = 0.025f;
    e.lowestHandY = 1e9f;
    e.highestPropY = -1e9f;
    // Hands: sample their paths over one full repeat.
    for (int i = 0; i <= span * 100; ++i) {
        const double t = i / 100.0;
        for (int right = 0; right < 2; ++right) {
            const Vec3 p = ev.palm(right != 0, t);
            e.lowestHandY = std::min(e.lowestHandY, p.y - handThickness);
            e.halfWidth = std::max(e.halfWidth, std::fabs(p.x) + 0.06f);
        }
    }
    // Balls: the top of each flight is at the vertex of its parabola (or an endpoint).
    for (int b = 0; b < span; ++b) {
        const int v = ev.valueAt(b);
        if (v <= 0) continue;
        if (v == 2) {  // held: just rides the hand
            e.highestPropY = std::max(e.highestPropY, ev.ballInHand(isRightHandBeat(b), b).y + kBallRadius);
            continue;
        }
        const double catchTime = b + v - ev.catchDwell(b + v);
        const int kSamples = 64;
        for (int i = 0; i <= kSamples; ++i) {
            const Vec3 p = ev.flightPosition(b, v, b + (catchTime - b) * i / kSamples);
            e.highestPropY = std::max(e.highestPropY, p.y + kBallRadius);
            e.halfWidth = std::max(e.halfWidth, std::fabs(p.x) + kBallRadius);
        }
    }
    if (e.highestPropY < e.lowestHandY) e.highestPropY = e.lowestHandY + 0.2f;
    return e;
}

JugglerScene evaluateScene(const std::vector<int>& loop, const BallOrbits& orbits,
                           const JuggleParams& params, double t) {
    JugglerScene scene;
    if (loop.empty()) return scene;
    const Evaluator ev(loop, params);
    scene.palmRight = ev.palm(true, t);
    scene.palmLeft = ev.palm(false, t);

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
            if (t >= holdStart && t < b) scene.balls.push_back({ball, ev.ballInHand(right, t), false});
        }

        if (v == 2) {
            // A 2 stays in the hand until it's thrown again two beats later.
            if (t >= b && t < b + 2) scene.balls.push_back({ball, ev.ballInHand(right, t), false});
            continue;
        }

        // In flight, and the trail it leaves (half the flight long, fading with age).
        const double catchTime = b + v - ev.catchDwell(b + v);
        if (t >= b && t < catchTime) scene.balls.push_back({ball, ev.flightPosition(b, v, t), true});
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
