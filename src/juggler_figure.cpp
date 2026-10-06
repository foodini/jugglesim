// juggler_figure.cpp - see juggler_figure.h.
#include "juggler_figure.h"

#include <algorithm>
#include <cmath>

namespace {

// Mirror a right-side point to the left side (juggler's right is -X).
Vec3 mirrorX(Vec3 p) { return {-p.x, p.y, p.z}; }

void drawLimbSegment(Renderer& r, const Primitives& prims, const Mat4& placement, Vec3 a, Vec3 b,
                     float radius, Vec3 color) {
    r.drawMesh(prims.cylinder, placement * segmentTransform(a, b, radius, radius), color);
}

void drawJoint(Renderer& r, const Primitives& prims, const Mat4& placement, Vec3 p, float radius,
               Vec3 color) {
    r.drawMesh(prims.sphere, placement * Mat4::translation(p) * Mat4::scaling({radius, radius, radius}),
               color);
}

// A flat box extending from the wrist along `handForward`, palm facing palmNormal.
void drawHand(Renderer& r, const Primitives& prims, const Mat4& placement, Vec3 handForward,
              Vec3 wrist, Vec3 palmNormal, Vec3 color) {
    const float handLength = 0.10f, handWidth = 0.075f, handThickness = 0.025f;
    const Vec3 n = normalize(palmNormal);
    Vec3 forward = normalize(handForward - n * dot(handForward, n));
    if (length(forward) < 0.5f) forward = Vec3(0.0f, 0.0f, 1.0f);
    Vec3 side = cross(n, forward);
    Vec3 center = wrist + forward * (handLength * 0.5f);
    Mat4 model = Mat4::fromBasis(side * handWidth, n * handThickness, forward * handLength, center);
    r.drawMesh(prims.cube, placement * model, color);
}

}  // namespace

JugglerPose makeNeutralPose() {
    JugglerPose p;
    p.head = {0.0f, 1.63f, 0.0f};
    p.headRadius = 0.11f;
    p.neckBase = {0.0f, 1.46f, 0.0f};
    p.waist = {0.0f, 0.98f, 0.0f};

    p.shoulderR = {-0.19f, 1.43f, 0.0f};
    p.elbowR = {-0.22f, 1.13f, 0.04f};
    p.wristR = {-0.20f, 1.09f, 0.31f};  // forearm roughly horizontal, pointing forward
    p.palmNormalR = {0.0f, 1.0f, 0.0f};

    p.hipR = {-0.11f, 0.95f, 0.0f};
    p.kneeR = {-0.12f, 0.51f, 0.03f};
    p.ankleR = {-0.13f, 0.06f, 0.0f};

    p.shoulderL = mirrorX(p.shoulderR);
    p.elbowL = mirrorX(p.elbowR);
    p.wristL = mirrorX(p.wristR);
    p.palmNormalL = p.palmNormalR;
    p.hipL = mirrorX(p.hipR);
    p.kneeL = mirrorX(p.kneeR);
    p.ankleL = mirrorX(p.ankleR);
    return p;
}

namespace {

// Two-bone IK: returns the elbow position for a chain from `root` to `target` with bone lengths
// a (root to elbow) and b (elbow to target), bending toward `pole`.
Vec3 solveElbow(Vec3 root, Vec3 target, float a, float b, Vec3 pole) {
    Vec3 toTarget = target - root;
    float d = length(toTarget);
    const float minReach = std::fabs(a - b) + 1e-4f;
    const float maxReach = a + b - 1e-4f;
    d = d < minReach ? minReach : (d > maxReach ? maxReach : d);
    const Vec3 dir = normalize(toTarget);
    // Distance along dir to the point under the elbow, and the elbow's height above that line.
    const float along = (a * a + d * d - b * b) / (2.0f * d);
    const float up = std::sqrt(std::max(0.0f, a * a - along * along));
    Vec3 bend = pole - dir * dot(pole, dir);
    if (length(bend) < 1e-5f) bend = Vec3(0.0f, -1.0f, 0.0f) - dir * dot(Vec3(0.0f, -1.0f, 0.0f), dir);
    return root + dir * along + normalize(bend) * up;
}

}  // namespace

void poseBody(JugglerPose& p, const BodyMotion& m) {
    const JugglerPose neutral = makeNeutralPose();
    // Upper body: moved and tilted about the waist. Elbows and wrists come along (the arms are
    // re-solved afterwards anyway, but this keeps their bone lengths).
    p.waist = kNeutralWaist + m.pelvisOffset;
    p.neckBase = bodyPoint(m, neutral.neckBase);
    p.shoulderR = bodyPoint(m, neutral.shoulderR);
    p.shoulderL = bodyPoint(m, neutral.shoulderL);
    p.elbowR = bodyPoint(m, neutral.elbowR);
    p.elbowL = bodyPoint(m, neutral.elbowL);
    p.wristR = bodyPoint(m, neutral.wristR);
    p.wristL = bodyPoint(m, neutral.wristL);

    // Head: the face turns toward the target, limited to what a neck does comfortably, and the
    // head tips back (or forward) with about half of that, pivoting at the top of the neck.
    const Vec3 neckTop = bodyPoint(m, neutral.head - Vec3(0.0f, neutral.headRadius, 0.0f));
    const Vec3 toTarget = m.lookAt - (neckTop + Vec3(0.0f, neutral.headRadius, 0.0f));
    const float flat = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    const float yaw = std::clamp(std::atan2(toTarget.x, std::max(toTarget.z, 0.05f)), -0.7f, 0.7f);
    const float pitch = std::clamp(std::atan2(toTarget.y, std::max(flat, 0.05f)), -0.6f, 1.2f);
    p.headForward = Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    const float tip = 0.5f * pitch;  // up is back (-Z)
    p.head = neckTop + Vec3(0.0f, std::cos(tip), -std::sin(tip)) * neutral.headRadius;

    // Legs: hips move with the pelvis, the ankles stay put, the knees bend forward (and a
    // little out) to make up the difference.
    p.hipR = neutral.hipR + m.pelvisOffset;
    p.hipL = neutral.hipL + m.pelvisOffset;
    p.ankleR = neutral.ankleR;
    p.ankleL = neutral.ankleL;
    const float thigh = length(neutral.kneeR - neutral.hipR);
    const float shin = length(neutral.ankleR - neutral.kneeR);
    p.kneeR = solveElbow(p.hipR, p.ankleR, thigh, shin, Vec3(-0.15f, 0.0f, 1.0f));
    p.kneeL = solveElbow(p.hipL, p.ankleL, thigh, shin, Vec3(0.15f, 0.0f, 1.0f));
}

void poseArmsForPalms(JugglerPose& p, Vec3 palmRight, Vec3 palmLeft, float elbowFlare) {
    // Palms face up; the fingers point the way the arm is reaching, seen from above (out from
    // the shoulder toward the palm), and the wrist sits half a hand-length back from the palm
    // center along that. The wrist bends as needed, so the palm stays level under the ball.
    auto handForward = [](Vec3 shoulder, Vec3 palm) {
        Vec3 flat(palm.x - shoulder.x, 0.0f, palm.z - shoulder.z);
        return length(flat) > 0.02f ? normalize(flat) : Vec3(0.0f, 0.0f, 1.0f);
    };
    p.handForwardR = handForward(p.shoulderR, palmRight);
    p.handForwardL = handForward(p.shoulderL, palmLeft);
    const float halfHand = 0.05f;
    const float upperR = length(p.elbowR - p.shoulderR), foreR = length(p.wristR - p.elbowR);
    const float upperL = length(p.elbowL - p.shoulderL), foreL = length(p.wristL - p.elbowL);
    // Elbows point down, a little out to the side and back (right arm is at -X); further out
    // with more flare.
    const float out = 0.5f + 0.9f * std::clamp(elbowFlare, 0.0f, 1.0f);
    const Vec3 poleR(-out, -1.0f, -0.3f), poleL(out, -1.0f, -0.3f);

    p.wristR = palmRight - p.handForwardR * halfHand;
    p.elbowR = solveElbow(p.shoulderR, p.wristR, upperR, foreR, poleR);
    p.wristR = p.elbowR + normalize(p.wristR - p.elbowR) * foreR;  // in case it was out of reach
    p.wristL = palmLeft - p.handForwardL * halfHand;
    p.elbowL = solveElbow(p.shoulderL, p.wristL, upperL, foreL, poleL);
    p.wristL = p.elbowL + normalize(p.wristL - p.elbowL) * foreL;
    p.palmNormalR = p.palmNormalL = Vec3(0.0f, 1.0f, 0.0f);
}

void drawJuggler(Renderer& r, const Primitives& prims, const JugglerPose& p, const Mat4& placement,
                 const JugglerStyle& style) {
    const Vec3 body = style.bodyColor;
    const float lr = style.limbRadius;

    // Head and neck.
    drawJoint(r, prims, placement, p.head, p.headRadius, body);
    Vec3 headBottom = p.head - Vec3(0.0f, p.headRadius * 0.8f, 0.0f);
    drawLimbSegment(r, prims, placement, p.neckBase - Vec3(0, 0.02f, 0), headBottom, lr * 1.1f, body);
    // Eyes, so you can see where the juggler is looking.
    {
        const Vec3 f = normalize(p.headForward);
        Vec3 side = cross(Vec3(0.0f, 1.0f, 0.0f), f);
        side = length(side) > 1e-3f ? normalize(side) : Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 up = cross(f, side);
        for (int i = -1; i <= 1; i += 2) {
            const Vec3 eye = p.head + f * (p.headRadius * 0.88f) + side * (0.038f * i) + up * 0.02f;
            drawJoint(r, prims, placement, eye, 0.017f, style.eyeColor);
        }
    }

    // Torso: inverted frustum from waist (narrow) to shoulder line (wide), flattened front-to-back.
    const float shoulderHalfWidth = 0.20f, torsoDepth = 0.10f;
    r.drawMesh(prims.torso,
               placement * segmentTransform(p.waist, p.neckBase, shoulderHalfWidth, torsoDepth),
               body);

    // Arms and hands.
    const Vec3 shoulders[2] = {p.shoulderR, p.shoulderL};
    const Vec3 elbows[2] = {p.elbowR, p.elbowL};
    const Vec3 wrists[2] = {p.wristR, p.wristL};
    const Vec3 palms[2] = {p.palmNormalR, p.palmNormalL};
    const Vec3 handForwards[2] = {p.handForwardR, p.handForwardL};
    for (int i = 0; i < 2; ++i) {
        drawJoint(r, prims, placement, shoulders[i], lr * 1.2f, body);
        drawLimbSegment(r, prims, placement, shoulders[i], elbows[i], lr, body);
        drawJoint(r, prims, placement, elbows[i], lr, body);
        drawLimbSegment(r, prims, placement, elbows[i], wrists[i], lr, body);
        drawHand(r, prims, placement, handForwards[i], wrists[i], palms[i], style.handColor);
    }

    // Hips and legs.
    drawLimbSegment(r, prims, placement, p.hipR, p.hipL, lr, body);
    if (style.drawLegs) {
        const Vec3 hips[2] = {p.hipR, p.hipL};
        const Vec3 knees[2] = {p.kneeR, p.kneeL};
        const Vec3 ankles[2] = {p.ankleR, p.ankleL};
        for (int i = 0; i < 2; ++i) {
            drawJoint(r, prims, placement, hips[i], lr, body);
            drawLimbSegment(r, prims, placement, hips[i], knees[i], lr, body);
            drawJoint(r, prims, placement, knees[i], lr, body);
            drawLimbSegment(r, prims, placement, knees[i], ankles[i], lr, body);
        }
    }
}
