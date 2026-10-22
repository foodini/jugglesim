// response_curve.h - a response curve: how a 3D mouse's reading on an axis (how far the cap is
// pushed, as a percentage of that axis's full push: see AppSettings::spaceFullPush) becomes a
// rate (how fast the camera moves), drawn as a handful of points
// (the curve editor in Edit > Preferences). Below the first point the axis does nothing (the
// dead zone); past the last it stays at the last point's rate; between, a smooth curve through
// the points that never overshoots them (a monotone cubic: where the points rise, the curve only
// rises).
#pragma once

#include <string>

struct ResponseCurve {
    static constexpr int kPoints = 6;
    float x[kPoints];  // readings (% of full push), increasing
    float y[kPoints];  // rates (the caller's units), at least 0

    bool operator==(const ResponseCurve& o) const;
    bool operator!=(const ResponseCurve& o) const { return !(*this == o); }
};

// The rate for a reading (its size: the sign is the caller's).
float responseRate(const ResponseCurve& c, float reading);

// The defaults: sliding (pixels a second, on the screen at the orbit point) and turning (degrees
// a second). Tuned by hand on a SpaceMouse; both rise from nothing at 1% (the device and its
// driver have a dead zone of their own), roughly as the push to the power 1.7.
ResponseCurve defaultSlideCurve();
ResponseCurve defaultTurnCurve();

// As saved ("12:30 22:70 31:120 49:300 61:550 69:1000"), and back. False if it isn't that (*c
// is left alone). A curve saved with fewer points (from before there were this many) gets points
// added where they're furthest apart, on the curve it had, so it keeps its shape.
std::string responseCurveText(const ResponseCurve& c);
bool readResponseCurve(const std::string& text, ResponseCurve* c);
