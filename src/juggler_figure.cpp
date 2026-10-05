// juggler_figure.cpp - see juggler_figure.h.
#include "juggler_figure.h"

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

// A flat box extending from the wrist along the forearm direction, palm facing palmNormal.
void drawHand(Renderer& r, const Primitives& prims, const Mat4& placement, Vec3 elbow, Vec3 wrist,
              Vec3 palmNormal, Vec3 color) {
    const float handLength = 0.10f, handWidth = 0.075f, handThickness = 0.025f;
    Vec3 forward = normalize(wrist - elbow);
    Vec3 n = normalize(palmNormal - forward * dot(palmNormal, forward));
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

void drawJuggler(Renderer& r, const Primitives& prims, const JugglerPose& p, const Mat4& placement,
                 const JugglerStyle& style) {
    const Vec3 body = style.bodyColor;
    const float lr = style.limbRadius;

    // Head and neck.
    drawJoint(r, prims, placement, p.head, p.headRadius, body);
    Vec3 headBottom = p.head - Vec3(0.0f, p.headRadius * 0.8f, 0.0f);
    drawLimbSegment(r, prims, placement, p.neckBase - Vec3(0, 0.02f, 0), headBottom, lr * 1.1f, body);

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
    for (int i = 0; i < 2; ++i) {
        drawJoint(r, prims, placement, shoulders[i], lr * 1.2f, body);
        drawLimbSegment(r, prims, placement, shoulders[i], elbows[i], lr, body);
        drawJoint(r, prims, placement, elbows[i], lr, body);
        drawLimbSegment(r, prims, placement, elbows[i], wrists[i], lr, body);
        drawHand(r, prims, placement, elbows[i], wrists[i], palms[i], style.handColor);
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
