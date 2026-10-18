// prop_figure.cpp - see prop_figure.h.
#include "prop_figure.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kClubSlices = 28;

// Builds a lathe from (distance from the top of the club, radius) pairs, in club space (+Y
// toward the top, center of mass at the origin).
Mesh clubPart(const std::vector<float>& fromTop, const std::vector<float>& radii) {
    std::vector<float> ys(fromTop.size());
    for (size_t i = 0; i < fromTop.size(); ++i) ys[i] = kClubCenterOfMass - fromTop[i];
    // makeLatheMesh wants heights increasing, so go from the knob end up.
    std::vector<float> y(ys.rbegin(), ys.rend()), r(radii.rbegin(), radii.rend());
    return makeLatheMesh(y.data(), r.data(), static_cast<int>(y.size()), kClubSlices);
}

}  // namespace

void PropMeshes::create() {
    // The club (515 mm): profile measured from the top (the big end), in meters.
    clubCap = clubPart({0.0f, 0.008f}, {0.0225f, 0.0225f});
    clubBody = clubPart({0.008f, 0.03f, 0.06f, 0.10f, 0.135f, 0.17f, 0.20f, 0.23f, 0.26f},
                        {0.024f, 0.031f, 0.037f, 0.0415f, 0.0425f, 0.040f, 0.033f, 0.023f, 0.0155f});
    clubCollar = clubPart({0.260f, 0.263f, 0.279f, 0.282f}, {0.0155f, 0.017f, 0.017f, 0.0145f});
    clubHandle = clubPart({0.282f, 0.40f, 0.492f}, {0.0145f, 0.0125f, 0.0105f});
    clubKnob = clubPart({0.492f, 0.497f, 0.503f, 0.509f, 0.515f},
                        {0.0100f, 0.0145f, 0.0170f, 0.0145f, 0.0100f});

    // Rings and hoops: the dash pattern laid around the middle of the band (or tube), repeated a
    // whole number of times, the dashes colored and the gaps dark. A hoop's dashes are longer,
    // in proportion, so it has about as many as a ring.
    struct Bands {
        std::vector<float> onStart, onEnd, offStart, offEnd;
    };
    auto bandsFor = [](DashPattern dash, float midRadius, float dashUnit) {
        Bands out;
        const float circumference = 2.0f * kPi * midRadius;
        const float* lengths = nullptr;
        int count = 0;
        dashPatternLengths(dash, &lengths, &count);
        if (count == 0) {
            // Solid: all color, with two narrow dark bands opposite each other.
            const float band = dashUnit / midRadius;  // radians
            out.onStart = {band * 0.5f, kPi + band * 0.5f};
            out.onEnd = {kPi - band * 0.5f, 2.0f * kPi - band * 0.5f};
            out.offStart = {-band * 0.5f, kPi - band * 0.5f};
            out.offEnd = {band * 0.5f, kPi + band * 0.5f};
            return out;
        }
        float patternLength = 0.0f;
        for (int i = 0; i < count; ++i) patternLength += lengths[i];
        const int repeats = std::max(1, static_cast<int>(std::lround(circumference / (patternLength * dashUnit))));
        const float radiansPerUnit = 2.0f * kPi / (static_cast<float>(repeats) * patternLength);
        float angle = 0.0f;
        for (int rep = 0; rep < repeats; ++rep)
            for (int i = 0; i < count; ++i) {
                const float next = angle + lengths[i] * radiansPerUnit;
                if (i % 2 == 0) {
                    out.onStart.push_back(angle);
                    out.onEnd.push_back(next);
                } else {
                    out.offStart.push_back(angle);
                    out.offEnd.push_back(next);
                }
                angle = next;
            }
        return out;
    };
    const float ringMid = 0.5f * (kRingInnerRadius + kRingOuterRadius);
    const float ringDashUnit = 0.03f;  // meters per dash unit, as on the trails
    for (int d = 0; d < static_cast<int>(DashPattern::Count); ++d) {
        const Bands ring = bandsFor(static_cast<DashPattern>(d), ringMid, ringDashUnit);
        ringColored[d] = makeRingSectorsMesh(kRingInnerRadius, kRingOuterRadius, kRingHalfThickness,
                                             ring.onStart.data(), ring.onEnd.data(), static_cast<int>(ring.onStart.size()));
        ringDark[d] = makeRingSectorsMesh(kRingInnerRadius, kRingOuterRadius, kRingHalfThickness,
                                          ring.offStart.data(), ring.offEnd.data(), static_cast<int>(ring.offStart.size()));
        const Bands hoop = bandsFor(static_cast<DashPattern>(d), kHoopRadius, ringDashUnit * kHoopRadius / ringMid);
        hoopColored[d] = makeTorusSectorsMesh(kHoopRadius, kHoopTubeRadius, hoop.onStart.data(), hoop.onEnd.data(),
                                              static_cast<int>(hoop.onStart.size()));
        hoopDark[d] = makeTorusSectorsMesh(kHoopRadius, kHoopTubeRadius, hoop.offStart.data(), hoop.offEnd.data(),
                                           static_cast<int>(hoop.offStart.size()));
    }
}

void PropMeshes::destroy() {
    destroyMesh(clubCap);
    destroyMesh(clubBody);
    destroyMesh(clubCollar);
    destroyMesh(clubHandle);
    destroyMesh(clubKnob);
    for (int d = 0; d < static_cast<int>(DashPattern::Count); ++d) {
        destroyMesh(ringColored[d]);
        destroyMesh(ringDark[d]);
        destroyMesh(hoopColored[d]);
        destroyMesh(hoopDark[d]);
    }
}

Vec3 propTrimColor(Vec3 color) {
    // Same hue, much darker, but lifted a little so it doesn't vanish into the background.
    return color * 0.38f + Vec3(0.06f, 0.06f, 0.07f);
}

void drawProp(Renderer& r, const PropMeshes& m, const Mesh& ballSphere, const BallState& prop,
              Vec3 color, DashPattern dash) {
    const Vec3 trim = propTrimColor(color);
    switch (prop.prop) {
        case PropType::Club: {
            const Vec3 y = normalize(prop.axis);
            const Vec3 x = normalize(prop.spinAxis);
            const Mat4 model = Mat4::fromBasis(x, y, cross(x, y), prop.center);
            r.drawMesh(m.clubBody, model, color);
            r.drawMesh(m.clubHandle, model, color);
            r.drawMesh(m.clubCap, model, trim);
            r.drawMesh(m.clubCollar, model, trim);
            r.drawMesh(m.clubKnob, model, trim);
            break;
        }
        case PropType::Ring:
        case PropType::Hoop: {
            // Local +X is `axis` (from the rim the hand holds toward the center), so the
            // bands turn with the ring; local +Y is the ring's normal.
            const Vec3 x = normalize(prop.axis);
            const Vec3 y = normalize(prop.spinAxis);
            const Mat4 model = Mat4::fromBasis(x, y, cross(x, y), prop.center);
            const int d = std::clamp(static_cast<int>(dash), 0, static_cast<int>(DashPattern::Count) - 1);
            const bool hoop = prop.prop == PropType::Hoop;
            r.drawMesh(hoop ? m.hoopColored[d] : m.ringColored[d], model, color);
            r.drawMesh(hoop ? m.hoopDark[d] : m.ringDark[d], model, trim);
            break;
        }
        default:
            r.drawMesh(ballSphere,
                       Mat4::translation(prop.center) * Mat4::scaling({kBallRadius, kBallRadius, kBallRadius}),
                       color);
            break;
    }
}
