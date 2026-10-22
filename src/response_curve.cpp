// response_curve.cpp - see response_curve.h.

#include "response_curve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

bool ResponseCurve::operator==(const ResponseCurve& o) const {
    for (int i = 0; i < kPoints; ++i)
        if (x[i] != o.x[i] || y[i] != o.y[i]) return false;
    return true;
}

namespace {

// responseRate, for the first n points of x and y (n at least 2, at most ResponseCurve::kPoints).
float rateThrough(const float* x, const float* y, int n, float reading) {
    const float r = std::fabs(reading);
    if (r < x[0]) return 0.0f;
    if (r >= x[n - 1]) return y[n - 1];
    int k = 0;
    while (k < n - 2 && r >= x[k + 1]) ++k;
    // Tangents by Fritsch and Carlson: the secants' average, made 0 at a peak or valley (where
    // the secants change sign or one is flat), and kept small enough not to overshoot.
    float secant[ResponseCurve::kPoints - 1] = {};
    for (int i = 0; i < n - 1; ++i) {
        const float dx = std::max(x[i + 1] - x[i], 1e-6f);
        secant[i] = (y[i + 1] - y[i]) / dx;
    }
    auto tangent = [&](int i) {
        if (i == 0) return secant[0];
        if (i == n - 1) return secant[n - 2];
        const float a = secant[i - 1], b = secant[i];
        if (a * b <= 0.0f) return 0.0f;
        return 2.0f / (1.0f / a + 1.0f / b);  // (the harmonic mean: never more than 3 times either)
    };
    const float h = std::max(x[k + 1] - x[k], 1e-6f);
    const float t = (r - x[k]) / h;
    float m0 = tangent(k), m1 = tangent(k + 1);
    // (At the ends, a one-sided tangent could overshoot: kept within three times the secant.)
    const float s = secant[k];
    if (s == 0.0f) {
        m0 = m1 = 0.0f;
    } else {
        m0 = std::clamp(m0 / s, 0.0f, 3.0f) * s;
        m1 = std::clamp(m1 / s, 0.0f, 3.0f) * s;
    }
    const float t2 = t * t, t3 = t2 * t;
    const float rate = (2.0f * t3 - 3.0f * t2 + 1.0f) * y[k] + (t3 - 2.0f * t2 + t) * h * m0 +
                       (-2.0f * t3 + 3.0f * t2) * y[k + 1] + (t3 - t2) * h * m1;
    return std::max(rate, 0.0f);
}

}  // namespace

float responseRate(const ResponseCurve& c, float reading) {
    return rateThrough(c.x, c.y, ResponseCurve::kPoints, reading);
}

ResponseCurve defaultSlideCurve() {
    return ResponseCurve{{1.0f, 22.4f, 42.4f, 63.2f, 79.2f, 99.0f}, {0.0f, 91.6f, 209.5f, 407.7f, 664.9f, 1147.0f}};
}

ResponseCurve defaultTurnCurve() {
    return ResponseCurve{{1.0f, 19.6f, 36.8f, 55.5f, 75.8f, 98.9f}, {0.0f, 5.0f, 12.9f, 26.0f, 48.7f, 85.6f}};
}

std::string responseCurveText(const ResponseCurve& c) {
    std::string text;
    for (int i = 0; i < ResponseCurve::kPoints; ++i) {
        char point[64];
        std::snprintf(point, sizeof(point), "%s%g:%g", i == 0 ? "" : " ", c.x[i], c.y[i]);
        text += point;
    }
    return text;
}

bool readResponseCurve(const std::string& text, ResponseCurve* c) {
    constexpr int kMax = ResponseCurve::kPoints;
    std::istringstream in(text);
    float x[kMax], y[kMax];
    int n = 0;
    std::string point;
    while (in >> point) {
        if (n == kMax) return false;  // (too many)
        const size_t colon = point.find(':');
        if (colon == std::string::npos) return false;
        char* end = nullptr;
        x[n] = std::strtof(point.c_str(), &end);
        if (end != point.c_str() + colon) return false;
        y[n] = std::strtof(point.c_str() + colon + 1, &end);
        if (*end != '\0') return false;
        if (!std::isfinite(x[n]) || !std::isfinite(y[n]) || y[n] < 0.0f) return false;
        if (n > 0 && x[n] <= x[n - 1]) return false;
        ++n;
    }
    if (n < 2) return false;
    // Fewer points than now: added where they're furthest apart, on the curve as it was.
    ResponseCurve read;
    int have = n;
    std::copy(x, x + n, read.x);
    std::copy(y, y + n, read.y);
    while (have < kMax) {
        int widest = 0;
        for (int i = 1; i < have - 1; ++i)
            if (read.x[i + 1] - read.x[i] > read.x[widest + 1] - read.x[widest]) widest = i;
        const float mx = std::round((read.x[widest] + read.x[widest + 1]) * 0.5f);
        if (mx <= read.x[widest] || mx >= read.x[widest + 1]) return false;  // (too close together to add one)
        const float my = rateThrough(x, y, n, mx);
        for (int i = have; i > widest + 1; --i) {
            read.x[i] = read.x[i - 1];
            read.y[i] = read.y[i - 1];
        }
        read.x[widest + 1] = mx;
        read.y[widest + 1] = my;
        ++have;
    }
    *c = read;
    return true;
}
