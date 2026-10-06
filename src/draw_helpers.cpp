// draw_helpers.cpp - see draw_helpers.h.
#include "draw_helpers.h"

#include <cmath>
#include <vector>

namespace {

ImVec2 lerp(ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }

float distance(ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Dash patterns as alternating on/off lengths in dash units. Empty = solid.
struct DashSpec {
    const float* lengths;
    int count;
};

DashSpec dashSpec(DashPattern dash) {
    DashSpec spec{nullptr, 0};
    dashPatternLengths(dash, &spec.lengths, &spec.count);
    return spec;
}

// Draws a polyline with a dash pattern applied along its length.
void drawDashedPolyline(ImDrawList* dl, const std::vector<ImVec2>& points, ImU32 color,
                        float thickness, DashPattern dash, float dashUnit) {
    if (points.size() < 2) return;
    const DashSpec spec = dashSpec(dash);
    if (spec.count == 0 || dashUnit <= 0.0f) {
        dl->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None,
                        thickness);
        return;
    }

    std::vector<ImVec2> run;  // the "on" stretch currently being collected
    int element = 0;
    float remaining = spec.lengths[0] * dashUnit;  // length left in the current dash element
    bool on = true;
    run.push_back(points[0]);

    for (size_t i = 1; i < points.size(); ++i) {
        const ImVec2 a = points[i - 1], b = points[i];
        const float segmentLength = distance(a, b);
        float used = 0.0f;
        while (segmentLength - used > remaining) {
            used += remaining;
            const ImVec2 p = lerp(a, b, used / segmentLength);
            if (on) {
                run.push_back(p);
                dl->AddPolyline(run.data(), static_cast<int>(run.size()), color, ImDrawFlags_None,
                                thickness);
                run.clear();
            } else {
                run.clear();
                run.push_back(p);
            }
            on = !on;
            element = (element + 1) % spec.count;
            remaining = spec.lengths[element] * dashUnit;
        }
        remaining -= segmentLength - used;
        if (on) run.push_back(b);
    }
    if (on && run.size() >= 2)
        dl->AddPolyline(run.data(), static_cast<int>(run.size()), color, ImDrawFlags_None,
                        thickness);
}

void fillShape(ImDrawList* dl, ImVec2 c, float r, MarkerShape shape, ImU32 color) {
    switch (shape) {
        case MarkerShape::Circle:
            dl->AddCircleFilled(c, r, color);
            break;
        case MarkerShape::Square: {
            const float h = r * 0.85f;
            dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), color);
            break;
        }
        case MarkerShape::TriangleUp:
            dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 1.15f), ImVec2(c.x + r, c.y + r * 0.7f),
                                  ImVec2(c.x - r, c.y + r * 0.7f), color);
            break;
        case MarkerShape::Diamond: {
            const float d = r * 1.2f;
            dl->AddQuadFilled(ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d),
                              ImVec2(c.x - d, c.y), color);
            break;
        }
        case MarkerShape::TriangleDown:
            dl->AddTriangleFilled(ImVec2(c.x, c.y + r * 1.15f), ImVec2(c.x - r, c.y - r * 0.7f),
                                  ImVec2(c.x + r, c.y - r * 0.7f), color);
            break;
        case MarkerShape::Hexagon:
            dl->AddNgonFilled(c, r * 1.05f, color, 6);
            break;
        default:
            dl->AddCircleFilled(c, r, color);
            break;
    }
}

}  // namespace

ImVec2 bezierPoint(ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, float t) {
    const float u = 1.0f - t;
    const float w0 = u * u * u, w1 = 3.0f * u * u * t, w2 = 3.0f * u * t * t, w3 = t * t * t;
    return ImVec2(w0 * p0.x + w1 * c1.x + w2 * c2.x + w3 * p1.x,
                  w0 * p0.y + w1 * c1.y + w2 * c2.y + w3 * p1.y);
}

float bezierDistance(ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, ImVec2 point, float* tNearest) {
    // Coarse sampling, then refine around the best sample.
    const int kSamples = 48;
    float bestT = 0.0f;
    float bestD = distance(p0, point);
    for (int i = 1; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / kSamples;
        const float d = distance(bezierPoint(p0, c1, c2, p1, t), point);
        if (d < bestD) {
            bestD = d;
            bestT = t;
        }
    }
    const float step = 1.0f / kSamples;
    for (int i = -8; i <= 8; ++i) {
        const float t = bestT + step * static_cast<float>(i) / 8.0f;
        if (t < 0.0f || t > 1.0f) continue;
        const float d = distance(bezierPoint(p0, c1, c2, p1, t), point);
        if (d < bestD) {
            bestD = d;
            bestT = t;
        }
    }
    if (tNearest) *tNearest = bestT;
    return bestD;
}

ImU32 mixColor(ImU32 a, ImU32 b, float t, float alpha) {
    const ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
    const float clampedAlpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                                 ca.z + (cb.z - ca.z) * t, clampedAlpha));
}

void drawBezierHalfHighlight(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1,
                             bool departureHalf, ImU32 color, float intensity) {
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const int kSegments = 24;
    // Walk from the chosen end (strength 1) toward the midpoint (strength 0).
    ImVec2 prev = bezierPoint(p0, c1, c2, p1, departureHalf ? 0.0f : 1.0f);
    for (int i = 1; i <= kSegments; ++i) {
        const float along = 0.5f * static_cast<float>(i) / kSegments;  // 0 .. 0.5
        const float t = departureHalf ? along : 1.0f - along;
        const ImVec2 p = bezierPoint(p0, c1, c2, p1, t);
        const float f = 1.0f - static_cast<float>(i - 1) / kSegments;  // strength, 1 -> ~0
        const float s = f * f * intensity;
        dl->AddLine(prev, p, mixColor(color, white, 0.6f, 0.22f * s), 4.0f + 10.0f * f);  // halo
        dl->AddLine(prev, p, mixColor(color, white, 0.35f, 0.9f * s), 2.5f + 2.5f * f);   // core
        prev = p;
    }
    // A ring around the endpoint that would be picked up.
    const ImVec2 endPoint = departureHalf ? p0 : p1;
    dl->AddCircle(endPoint, 9.0f, mixColor(color, white, 0.5f, 0.9f * intensity), 0, 2.0f);
}

void drawHeldThrowEffects(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1,
                          ImU32 color, float time, float intensity) {
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const float pulse = 0.5f + 0.5f * std::sin(time * 5.0f);

    // Pulsing glow: a few wide translucent strokes under the curve.
    const int kSamples = 48;
    std::vector<ImVec2> points;
    points.reserve(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i)
        points.push_back(bezierPoint(p0, c1, c2, p1, static_cast<float>(i) / kSamples));
    const int n = static_cast<int>(points.size());
    dl->AddPolyline(points.data(), n, mixColor(color, white, 0.3f, (0.10f + 0.10f * pulse) * intensity),
                    ImDrawFlags_None, 22.0f + 6.0f * pulse);
    dl->AddPolyline(points.data(), n, mixColor(color, white, 0.4f, (0.18f + 0.12f * pulse) * intensity),
                    ImDrawFlags_None, 12.0f + 3.0f * pulse);
    dl->AddPolyline(points.data(), n, mixColor(color, white, 0.5f, 0.95f * intensity),
                    ImDrawFlags_None, 4.0f);

    // Sparks: bright dots running from the departure toward the arrival, each with a short
    // fading trail.
    const int kSparks = 7;
    const int kTrail = 5;
    for (int i = 0; i < kSparks; ++i) {
        const float head = std::fmod(time * 0.55f + static_cast<float>(i) / kSparks, 1.0f);
        for (int j = 0; j < kTrail; ++j) {
            const float t = head - 0.018f * static_cast<float>(j);
            if (t < 0.0f) break;
            const float fade = 1.0f - static_cast<float>(j) / kTrail;
            const ImVec2 p = bezierPoint(p0, c1, c2, p1, t);
            dl->AddCircleFilled(p, 1.2f + 2.3f * fade, mixColor(color, white, 0.85f, 0.95f * fade * intensity));
        }
    }
}

void drawMarker(ImDrawList* dl, ImVec2 center, float radius, MarkerShape shape, ImU32 color,
                float outlineWidth, ImU32 outlineColor) {
    if (outlineWidth > 0.0f) fillShape(dl, center, radius + outlineWidth, shape, outlineColor);
    fillShape(dl, center, radius, shape, color);
}

void drawStyledBezier(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, ImU32 color,
                      float thickness, DashPattern dash, float dashUnit) {
    if (dash == DashPattern::Solid) {
        dl->AddBezierCubic(p0, c1, c2, p1, color, thickness);
        return;
    }
    const int kSamples = 64;
    std::vector<ImVec2> points;
    points.reserve(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i)
        points.push_back(bezierPoint(p0, c1, c2, p1, static_cast<float>(i) / kSamples));
    drawDashedPolyline(dl, points, color, thickness, dash, dashUnit);
}

void drawStyledLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float thickness,
                    DashPattern dash, float dashUnit) {
    const std::vector<ImVec2> points = {a, b};
    drawDashedPolyline(dl, points, color, thickness, dash, dashUnit);
}

void drawBezierArrowHead(ImDrawList* dl, ImVec2 p0, ImVec2 c1, ImVec2 c2, ImVec2 p1, ImU32 color,
                         float size, float inset) {
    // Walk back from the end of the curve until we've covered `inset`, then once more by
    // `size` to get the direction the curve is travelling at the tip.
    const int kSamples = 64;
    ImVec2 prev = p1;
    float travelled = 0.0f;
    ImVec2 tip = p1;
    bool haveTip = inset <= 0.0f;
    for (int i = kSamples - 1; i >= 0; --i) {
        const ImVec2 p = bezierPoint(p0, c1, c2, p1, static_cast<float>(i) / kSamples);
        const float step = distance(prev, p);
        if (!haveTip && travelled + step >= inset) {
            tip = lerp(prev, p, (inset - travelled) / step);
            haveTip = true;
        }
        travelled += step;
        prev = p;
        if (haveTip && travelled >= inset + size) {
            drawArrowHead(dl, tip, p, color, size);
            return;
        }
    }
    if (haveTip) drawArrowHead(dl, tip, p0, color, size);  // very short curve
}

void drawArrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 from, ImU32 color, float size) {
    float dx = tip.x - from.x, dy = tip.y - from.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    dx /= len;
    dy /= len;
    const ImVec2 base(tip.x - dx * size, tip.y - dy * size);
    const ImVec2 left(base.x - dy * size * 0.5f, base.y + dx * size * 0.5f);
    const ImVec2 right(base.x + dy * size * 0.5f, base.y - dx * size * 0.5f);
    dl->AddTriangleFilled(tip, left, right, color);
}
