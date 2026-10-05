// math3d.h - minimal vector/matrix math for JuggleSim.
// Matrices are column-major (OpenGL convention): element (row, col) is m[col * 4 + row].
#pragma once

#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float len = length(a);
    return len > 1e-8f ? a * (1.0f / len) : Vec3(0.0f, 0.0f, 0.0f);
}

struct Mat4 {
    float m[16] = {};

    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    static Mat4 identity() {
        Mat4 r;
        r.at(0, 0) = r.at(1, 1) = r.at(2, 2) = r.at(3, 3) = 1.0f;
        return r;
    }

    // Matrix whose columns are the given axes, translated to origin.
    static Mat4 fromBasis(Vec3 xAxis, Vec3 yAxis, Vec3 zAxis, Vec3 origin) {
        Mat4 r = identity();
        r.at(0, 0) = xAxis.x; r.at(1, 0) = xAxis.y; r.at(2, 0) = xAxis.z;
        r.at(0, 1) = yAxis.x; r.at(1, 1) = yAxis.y; r.at(2, 1) = yAxis.z;
        r.at(0, 2) = zAxis.x; r.at(1, 2) = zAxis.y; r.at(2, 2) = zAxis.z;
        r.at(0, 3) = origin.x; r.at(1, 3) = origin.y; r.at(2, 3) = origin.z;
        return r;
    }

    static Mat4 translation(Vec3 t) { return fromBasis({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, t); }

    static Mat4 scaling(Vec3 s) { return fromBasis({s.x, 0, 0}, {0, s.y, 0}, {0, 0, s.z}, {}); }

    // Rotation about the +Y axis (useful for turning a juggler to face a direction).
    static Mat4 rotationY(float radians) {
        float c = std::cos(radians), s = std::sin(radians);
        return fromBasis({c, 0, -s}, {0, 1, 0}, {s, 0, c}, {});
    }

    static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
        Mat4 r;
        float f = 1.0f / std::tan(fovYRadians * 0.5f);
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
        r.at(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
        r.at(3, 2) = -1.0f;
        return r;
    }

    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.at(0, 0) = s.x;  r.at(0, 1) = s.y;  r.at(0, 2) = s.z;  r.at(0, 3) = -dot(s, eye);
        r.at(1, 0) = u.x;  r.at(1, 1) = u.y;  r.at(1, 2) = u.z;  r.at(1, 3) = -dot(u, eye);
        r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z; r.at(2, 3) = dot(f, eye);
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, col);
            r.at(row, col) = sum;
        }
    return r;
}

inline Vec3 transformPoint(const Mat4& m, Vec3 p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
            m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3)};
}

// Model matrix that maps a unit primitive whose axis runs from y=0 to y=1 (cylinder, frustum)
// onto the segment a->b, with the given radii across the segment.
inline Mat4 segmentTransform(Vec3 a, Vec3 b, float radiusX, float radiusZ) {
    Vec3 axis = b - a;
    float len = length(axis);
    Vec3 yDir = len > 1e-8f ? axis * (1.0f / len) : Vec3(0, 1, 0);
    Vec3 helper = std::fabs(yDir.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    // Prefer keeping the cross-section's X axis horizontal for vertical-ish segments.
    if (std::fabs(yDir.y) >= 0.99f) helper = Vec3(0, 0, 1);
    Vec3 xDir = normalize(cross(yDir, helper));
    Vec3 zDir = cross(xDir, yDir);
    return Mat4::fromBasis(xDir * radiusX, yDir * len, zDir * radiusZ, a);
}
